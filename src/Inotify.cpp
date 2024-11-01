#include "include/Inotify.hpp"
#include "include/FileEvent.hpp"
#include <cstring>
#include <sstream>
#include <sys/inotify.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace inotify {

Inotify::Inotify(const std::filesystem::path &path, const std::vector<std::string> &ignored)
  : _root(path), _ignored_dirs(ignored)
{
  std::stringstream error_stream;

  // Initialize the inotify instance
  _inotify_fd = inotify_init();
  if (_inotify_fd < 0)
  {
    error_stream << "Failed to initialize inotify: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }

  // Initialize the event file descriptor for interrupting
  _event_fd = eventfd(0, EFD_NONBLOCK);
  if (_event_fd < 0)
  {
    error_stream << "Failed to initialize event file descriptor: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }

  // Initialize the epoll instance
  _epoll_fd = epoll_create1(0);
  if (_epoll_fd < 0)
  {
    error_stream << "Failed to initialize epoll instance: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }

  // Register inotify file descriptor with epoll
  _inotify_epoll_event.events = EPOLLIN;
  _inotify_epoll_event.data.fd = _inotify_fd;

  if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _inotify_fd, &_inotify_epoll_event) == -1)
  {
    error_stream << "Failed to add inotify file descriptor to epoll: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }

  // Register event file descriptor with epoll for interrupts
  _stop_epoll_event.events = EPOLLIN;
  _stop_epoll_event.data.fd = _event_fd;

  if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _event_fd, &_stop_epoll_event) == -1)
  {
    error_stream << "Failed to add event file descriptor to epoll: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }

  // initialize by watching the specified directory
  watchDirectory(path);
  _logger.logEvent("Watching directory: %s", path.c_str());
}

Inotify::~Inotify()
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
      _logger.logEvent("Nothing to watch. Bye!");
      _stopped = true;
      return;
    }

    if (event.mask & IN_ISDIR)
    {
      processDirectoryEvent(event);
    }
    else
    {
      processFileEvent(event);
    }
  }
}

void Inotify::stop()
{
  _stopped = true;

  /* Write a signal to the eventfd to interrupt the epoll_wait call */
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

void Inotify::watchDirectory(const std::filesystem::path &path)
{
  if (!std::filesystem::is_directory(path) || isIgnored(path)) return;

  addWatch(path);
  for (const auto &entry : std::filesystem::recursive_directory_iterator(path))
  {
    if (entry.is_directory() && !isIgnored(entry.path()))
    {
      addWatch(entry.path());
    }
  }
}

void Inotify::addWatch(const std::filesystem::path& path)
{
  // Watch for file creation, deletion, and modification events
  int flags = IN_MODIFY | IN_CREATE | IN_MOVE | IN_DELETE | IN_DONT_FOLLOW;
  if (_wd_cache.empty())
  {
    flags |= IN_MOVE_SELF | IN_DELETE_SELF;  // is this right?
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

void Inotify::rewriteCache()
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

  watchDirectory(_root);
  // if still empty for whatever reason after watching the root directory, then we are done
  if (_wd_cache.empty())
  {
    stop();
  }
}

void Inotify::invalidateSubdirectories(const std::filesystem::path &old_path)
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
  const std::filesystem::path &dir_path = _wd_cache.at(event.wd);
  const std::filesystem::path &full_path = dir_path / event.filename;

  if (event.mask & IN_DELETE)
  {
    _logger.logEvent("Deleted directory: %s", full_path.c_str());
  }
  else if (event.mask & (IN_CREATE | IN_MOVED_TO))
  {
    if (event.mask & IN_CREATE)
      _logger.logEvent("Created directory: %s", full_path.c_str());
    else
      _logger.logEvent("Moved into watch directory: %s", full_path.c_str());

    watchDirectory(full_path);
  }
  else if (event.mask & IN_MOVED_FROM)
  {
    // We assume that the next event is the corresponding IN_MOVED_TO event
    if (_event_queue.empty())
    {
      _logger.logEvent("Moved out of watch directory: %s", full_path.c_str());
      invalidateSubdirectories(full_path);
    }
    else
    {
      const auto next_event = _event_queue.front();
      _event_queue.pop();
      if (next_event.mask & IN_MOVED_TO && next_event.cookie == event.cookie)
      {
        const std::filesystem::path &next_dir_path = _wd_cache.at(next_event.wd);
        const std::filesystem::path &next_full_path = next_dir_path / next_event.filename;
        if (dir_path == next_dir_path)
        {
          _logger.logEvent("Renamed directory: %s -> %s", full_path.c_str(), next_full_path.c_str());
        }
        else
        {
          _logger.logEvent("Moved directory: %s -> %s", full_path.c_str(), next_full_path.c_str());
        }

        invalidateSubdirectories(full_path);
        watchDirectory(next_full_path);
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
        {
          _logger.logEvent("Renamed file: %s -> %s", full_path.c_str(), next_full_path.c_str());
        }
        else
        {
          _logger.logEvent("Moved file: %s -> %s", full_path.c_str(), next_full_path.c_str());
        }
      }
    }
  }
}

}  // namespace inotify
