#include "include/Inotify.hpp"
#include "include/FileEvent.hpp"
#include "include/InotifyError.hpp"
#include <cstring>
#include <sstream>
#include <sys/inotify.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace inotify {

Inotify::Inotify(const std::filesystem::path &path, const std::vector<std::string> &ignored)
  : _root(path), _ignored_dirs(ignored), _logger{}
{
  initialize();
  if (!watchDirectory(path)) throw std::invalid_argument("Failed to watch directory: " + path.string());
}

Inotify::~Inotify() { terminate(); }

void Inotify::initialize()
{
  // Initialize the inotify instance
  _inotify_fd = inotify_init();
  if (_inotify_fd < 0) throw InotifyError("Failed to initialize inotify");

  // Initialize the event file descriptor for interrupting
  _event_fd = eventfd(0, EFD_NONBLOCK);
  if (_event_fd < 0) throw InotifyError("Failed to initialize event file descriptor");

  // Initialize the epoll instance
  _epoll_fd = epoll_create1(0);
  if (_epoll_fd < 0) throw InotifyError("Failed to initialize epoll instance");

  // Register inotify file descriptor with epoll
  _inotify_epoll_event.events = EPOLLIN;
  _inotify_epoll_event.data.fd = _inotify_fd;

  if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _inotify_fd, &_inotify_epoll_event) == -1)
    throw InotifyError("Failed to add inotify file descriptor to epoll");

  // Register event file descriptor with epoll for interrupts
  _stop_epoll_event.events = EPOLLIN;
  _stop_epoll_event.data.fd = _event_fd;

  if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _event_fd, &_stop_epoll_event) == -1)
    throw InotifyError("Failed to add event file descriptor to epoll");
}

void Inotify::terminate() noexcept
{
  epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, _inotify_fd, 0);
  epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, _event_fd, 0);
  close(_inotify_fd);
  close(_epoll_fd);
  close(_event_fd);
}

void Inotify::run()
{
  _stopped = false;
  while (!_stopped)
  {
    runOnce();
  }
}

void Inotify::stop()
{
  _stopped = true;

  // Write a signal to the eventfd to interrupt the epoll_wait call
  uint64_t buffer = 1;
  write(_event_fd, &buffer, sizeof(buffer));
}

bool Inotify::isIgnored(const std::filesystem::path &path) const
{
  for (const auto &ignored : _ignored_dirs)
  {
    if (path == ignored) return true;
  }

  return false;
}

bool Inotify::watchDirectory(const std::filesystem::path &path)
{
  if (!std::filesystem::is_directory(path))
  {
    _logger.logError("Failed to watch directory: %s", path.c_str());
    return false;
  }

  if (isIgnored(path)) return true;

  addWatch(path);
  for (const auto &entry : std::filesystem::recursive_directory_iterator(path))
  {
    if (entry.is_directory() && !isIgnored(entry.path()))
    {
      addWatch(entry.path());
    }
  }

  return true;
}

void Inotify::addWatch(const std::filesystem::path &path)
{
  // Watch for file creation, deletion, and modification events
  int flags = IN_MODIFY | IN_CREATE | IN_MOVE | IN_DELETE | IN_DONT_FOLLOW;
  if (_wd_cache.empty())
  {
    flags |= IN_MOVE_SELF | IN_DELETE_SELF;
  }

  int wd = inotify_add_watch(_inotify_fd, path.c_str(), flags);
  if (wd == -1)
  {
    std::stringstream error_stream;
    error_stream << "Failed to watch directory: " << path << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }

  // Add the watch descriptor to the cache
  _wd_cache.insert(std::make_pair(wd, path));
}

bool Inotify::checkCacheConsistency(const FileEvent &event)
{
  // Cache consistency check
  if (_wd_cache.find(event.wd) == _wd_cache.end())
  {
    // cache reached inconsistent state; try to recover
    clearCache();  // remove all watches
    terminate();   // close all file descriptors
    initialize();  // reinitialize inotify and epoll

    // rewatch the root directory and its subdirectories
    if (!watchDirectory(_root)) throw InotifyError("Failed to recover from inconsistent cache state");

    std::queue<FileEvent> empty;
    std::swap(_event_queue, empty);  // clear the event queue

    _event_buffer.fill(0);  // clear the event buffer

    _logger.logEvent("Cache reached inconsistent state; recovered successfully.");

    return false;
  }

  return true;
}

void Inotify::runOnce()
{
  while (_event_queue.empty() && !_stopped)
  {
    ssize_t length = readEventsIntoBuffer();
    if (length > 0) readEventsFromBuffer(length);
  }

  while (!_event_queue.empty() && !_stopped)
  {
    FileEvent event = _event_queue.front();
    _event_queue.pop();

    if (event.mask & (IN_DELETE_SELF | IN_MOVE_SELF))
    {
      _stopped = true;
      _logger.logEvent("Nothing to watch.");
      return;
    }

    if (!checkCacheConsistency(event)) return;

    if (event.mask & IN_ISDIR)
      processDirectoryEvent(event);
    else
      processFileEvent(event);
  }
}

ssize_t Inotify::readEventsIntoBuffer()
{
  int triggered_events = epoll_wait(_epoll_fd, _epoll_events, MAX_EVENTS, -1);

  if (triggered_events == -1) return 0;

  ssize_t length = 0;

  for (int i = 0; i < triggered_events; ++i)
  {
    // Check if the event fd was triggered by the stop signal
    if (_epoll_events[i].data.fd == _event_fd)
    {
      break;  // Stop if stop signal received
    }

    // Check if the inotify fd was triggered
    if (_epoll_events[i].data.fd == _inotify_fd)
    {
      length = read(_inotify_fd, _event_buffer.data(), _event_buffer.size());  // Read events into buffer

      if (length == -1)
      {
        std::stringstream error_stream;
        error_stream << "Failed to read events: " << strerror(errno);
        throw std::runtime_error(error_stream.str());
      }
    }
  }

  return length;
}

void Inotify::readEventsFromBuffer(ssize_t length)
{
  uint8_t *event_buffer = _event_buffer.data();
  size_t i = 0;
  while (i < length)
  {
    struct inotify_event *event = (struct inotify_event *)&event_buffer[i];

    if (event->mask & IN_IGNORED)
    {
      // Watch was removed either explcitly or implicitly
      // The watch descriptor might no longer be in the cache, but nonetheless we try to erase it
      _wd_cache.erase(event->wd);
    }
    else
    {
      _event_queue.push(FileEvent(event));
    }

    i += EVENT_SIZE + event->len;
  }
}

void Inotify::clearCache()
{
  for (auto it = _wd_cache.begin(); it != _wd_cache.end();)
  {
    int result = inotify_rm_watch(_inotify_fd, it->first);
    if (result == -1)
    {
      std::stringstream error_stream;
      error_stream << "Failed to remove watch: " << strerror(errno);
      throw std::runtime_error(error_stream.str());
    }
    it = _wd_cache.erase(it);
  }
}

void Inotify::rewriteCachedPaths(const std::string &old_path_prefix, const std::string &new_path_prefix)
{
  for (auto &[wd, path] : _wd_cache)
  {
    if (path.string().rfind(old_path_prefix, 0) == 0)
    {
      std::string new_path_suffix = path.string().substr(old_path_prefix.size());
      path = new_path_prefix + new_path_suffix;
    }
  }
}

void Inotify::zapSubdirectories(const std::filesystem::path &old_path)
{
  // Collect watch descriptors to be removed
  std::vector<int> to_remove;

  for (const auto &[wd, path] : _wd_cache)
  {
    // Check if the path is a subdirectory of old_path
    if (path.string().rfind(old_path.string(), 0) == 0)
    {
      to_remove.push_back(wd);
    }
  }

  // Remove watches for the affected subdirectories
  for (int wd : to_remove)
  {
    _wd_cache.erase(wd);
    int result = inotify_rm_watch(_inotify_fd, wd);
    if (result == -1)
    {
      std::stringstream error_stream;
      error_stream << "Failed to remove watch: " << strerror(errno);
      throw std::runtime_error(error_stream.str());
    }
  }
}

void Inotify::processDirectoryEvent(const FileEvent &event)
{
  // The path of the directory that the event occurred in
  const std::filesystem::path &dir_path = _wd_cache.at(event.wd);
  // The path of the directory that the event is about
  const std::filesystem::path &full_path = dir_path / event.filename;

  if (event.mask & IN_DELETE)
  {
    _logger.logEvent("Deleted directory: %s", full_path.c_str());
    // No need to handle subdirectories since they will be implicitly removed via IN_IGNORED events
  }
  // A new subdirectory was created, or a subdirectory was renamed into the watch directory
  else if (event.mask & (IN_CREATE | IN_MOVED_TO))
  {
    _logger.logEvent("Created directory: %s", full_path.c_str());

    // Start watching the new subdirectory and its subdirectories
    watchDirectory(full_path);
  }
  // A subdirectory was renamed or moved out of the watch directory
  else if (event.mask & IN_MOVED_FROM)
  {
    // If no more events in the queue, we assume there will be no corresponding IN_MOVED_TO event
    if (_event_queue.empty())
    {
      _logger.logEvent("Moved out of watch directory: %s", full_path.c_str());
      zapSubdirectories(full_path);  // Remove watch descriptors and cache entries for subdirectories
    }
    else  // We assume that the next event is the corresponding IN_MOVED_TO event
    {
      const auto next_event = _event_queue.front();
      _event_queue.pop();

      if (next_event.mask & IN_MOVED_TO && next_event.cookie == event.cookie)
      {
        // The path of the directory that the next event occurred in
        const std::filesystem::path &next_dir_path = _wd_cache.at(next_event.wd);
        // The path of the directory that the next event is about
        const std::filesystem::path &next_full_path = next_dir_path / next_event.filename;

        if (dir_path == next_dir_path)  // If the directories are the same, it was a rename
          _logger.logEvent("Renamed directory: %s -> %s", full_path.c_str(), next_full_path.c_str());
        else  // otherwise it was a move
          _logger.logEvent("Moved directory: %s -> %s", full_path.c_str(), next_full_path.c_str());

        // Rewrite the cached paths
        rewriteCachedPaths(full_path, next_full_path);
      }
    }
  }
}

void Inotify::processFileEvent(const FileEvent &event)
{
  const std::filesystem::path &dir_path = _wd_cache.at(event.wd);
  const std::filesystem::path &full_path = dir_path / event.filename;

  if (event.mask & IN_CREATE)
    _logger.logEvent("Created file: %s", full_path.c_str());
  else if (event.mask & IN_DELETE)
    _logger.logEvent("Deleted file: %s", full_path.c_str());
  else if (event.mask & IN_MODIFY)
    _logger.logEvent("Modified file: %s", full_path.c_str());
  else if (event.mask & IN_MOVED_TO)
    _logger.logEvent("Moved file into watch directory: %s", full_path.c_str());
  else if (event.mask & IN_MOVED_FROM)
  {
    // We assume that the next event is the corresponding IN_MOVED_TO event
    if (_event_queue.empty())
    {
      _logger.logEvent("Moved file out of watch directory: %s", full_path.c_str());
    }
    else
    {
      const FileEvent next_event = _event_queue.front();
      _event_queue.pop();
      if (next_event.mask & IN_MOVED_TO && next_event.cookie == event.cookie)
      {
        const std::filesystem::path &next_dir_path = _wd_cache.at(next_event.wd);
        const std::filesystem::path &next_full_path = next_dir_path / next_event.filename;
        if (dir_path == next_dir_path)
          _logger.logEvent("Renamed file: %s -> %s", full_path.c_str(), next_full_path.c_str());
        else
          _logger.logEvent("Moved file: %s -> %s", full_path.c_str(), next_full_path.c_str());
      }
    }
  }
}
}  // namespace inotify
