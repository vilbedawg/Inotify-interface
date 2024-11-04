
#include "include/Inotify.hpp"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <cstring>
#include <stack>

#include "include/FileEvent.hpp"
#include "include/InotifyError.hpp"

namespace inotify {

/**
 * Constructor for initializing the Inotify watcher with a root path and a list of directories to ignore.
 * @param path The root directory to monitor for file system changes.
 * @param ignored_dirs List of directory names to be excluded from monitoring.
 * @throws std::invalid_argument if the root directory could not be watched.
 */
Inotify::Inotify(const std::filesystem::path &path, const std::vector<std::string> &ignored)
  : _root(path), _ignored_dirs(ignored), _logger{}
{
  initialize();
  if (!watchDirectory(path)) throw std::invalid_argument("Failed to watch directory: " + path.string());
}

/**
 * Destructor that cleans up resources related to inotify and epoll file descriptors.
 */
Inotify::~Inotify() { terminate(); }

/**
 * Initializes the inotify instance and sets up epoll for handling events.
 */
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

/**
 * Gracefully terminates the inotify instance, closing all file descriptors.
 */
void Inotify::terminate() noexcept
{
  epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, _inotify_fd, 0);
  epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, _event_fd, 0);
  close(_inotify_fd);
  close(_epoll_fd);
  close(_event_fd);
}

/**
 * Starts the inotify watcher, processing file events in an ongoing loop.
 */
void Inotify::run()
{
  _stopped = false;
  while (!_stopped)
  {
    runOnce();
  }
}

/**
 * Stops the inotify watcher by setting the _stopped flag and writing a signal to the eventfd.
 */
void Inotify::stop()
{
  _stopped = true;

  // Write a signal to the eventfd to interrupt the epoll_wait call
  uint64_t buffer = 1;
  write(_event_fd, &buffer, sizeof(buffer));
}

/**
 * Checks if a specified directory is in the ignored directories list.
 * @param path The directory path to check.
 * @return True if the path is in the ignored list, false otherwise.
 */
bool Inotify::isIgnored(const std::string &path_name) const
{
  for (const auto &ignored : _ignored_dirs)
  {
    if (path_name == ignored) return true;
  }

  return false;
}

/**
 * Adds a directory and all of it's subdirectories to the inotify watch list while doing some sanity checks.
 * @param path The directory path to monitor.
 * @return True if successfully added, false otherwise.
 */
bool Inotify::watchDirectory(const std::filesystem::path &path)
{
  std::stack<std::filesystem::path> dirs;
  if (!std::filesystem::is_directory(path))
  {
    _logger.logEvent("Failed to watch directory: %s", path.c_str());
    return false;
  }

  // Check if already watched
  for (const auto &[_, dir] : _wd_cache)
  {
    if (dir == path) return true;
  }

  // Check if the directory is in the ignored list
  if (isIgnored(path.filename())) return true;

  dirs.push(path);

  while (!dirs.empty())
  {
    std::filesystem::path dir = dirs.top();
    dirs.pop();
    if (addWatch(dir) == -1) return false;

    /* Using recursive_directory_iterator doesn't allow to properly
     * ignore the directories since it traverses into the subdirectories even if we ignore them
     * So we use directory_iterator and manually push the subdirectories onto the stack
     */
    for (const auto &entry : std::filesystem::directory_iterator(dir))
    {
      if (entry.is_directory() && !isIgnored(entry.path().filename()))
      {
        dirs.push(entry.path());
      }
    }
  }

  return true;
}

/**
 * Registers a specific directory path with inotify and adds it to the watch descriptor cache.
 * @param path The directory path to add to the inotify watch list.
 * @return The watch descriptor for the directory, or -1 if the watch could not be added.
 */
int Inotify::addWatch(const std::filesystem::path &path)
{
  // Watch for file creation, deletion, and modification events, and don't follow symbolic links
  int flags = IN_MODIFY | IN_CREATE | IN_MOVE | IN_DELETE | IN_DONT_FOLLOW;
  if (_wd_cache.empty())
  {
    flags |= IN_MOVE_SELF | IN_DELETE_SELF;  // Watch for the root directory being moved or deleted
  }

  int wd = inotify_add_watch(_inotify_fd, path.c_str(), flags);
  if (wd == -1)
  {
    _logger.logEvent("Failed to add watch for directory: %s", path.c_str());
    return -1;
  }

  // Add the watch descriptor to the cache
  _wd_cache.insert(std::make_pair(wd, path));
  return wd;
}

/**
 * Verifies that an event corresponds to the current state of the watch cache.
 * If the cache is inconsistent, it will attempt to recover by reinitializing the inotify instance and rewatching the
 * root directory. It will also clear the current event queue and event buffer.
 * @param event The file event to check for consistency.
 * @return True if the event matches the cache, false otherwise.
 * @throws InotifyError if the cache could not be recovered.
 */
bool Inotify::checkCacheConsistency(const FileEvent &event)
{
  // Check whether the watch descriptor is in the cache
  if (_wd_cache.find(event.wd) == _wd_cache.end())
  {
    // Cache reached inconsistent state; try to recover
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

/**
 * Processes a single iteration of file events, checking for new events and updating as needed.
 */
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

    // If the root directory that is watched is deleted or moved, stop watching
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

/**
 * Reads inotify events into the event buffer.
 * @return The number of bytes read into the buffer.
 * @throws InotifyError if the events could not be read.
 */
ssize_t Inotify::readEventsIntoBuffer()
{
  int triggered_events = epoll_wait(_epoll_fd, _epoll_events, MAX_EVENTS, -1);

  if (triggered_events == -1) return 0;

  ssize_t length = 0;

  for (int i = 0; i < triggered_events; ++i)
  {
    // Check if the event fd was triggered by the stop signal
    if (_epoll_events[i].data.fd == _event_fd) break;  // Stop if stop signal received

    // Check if the inotify fd was triggered
    if (_epoll_events[i].data.fd == _inotify_fd)
    {
      length = read(_inotify_fd, _event_buffer.data(), _event_buffer.size());  // Read events into buffer
      if (length == -1) throw InotifyError("Failed to read events from inotify");
    }
  }

  return length;
}

/**
 * Reads inotify events from the event buffer and pushes them onto the event queue.
 * @param length The number of bytes to read from the buffer.
 */
void Inotify::readEventsFromBuffer(ssize_t length)
{
  uint8_t *event_buffer = _event_buffer.data();
  size_t i = 0;
  while (i < length)
  {
    struct inotify_event *event = (struct inotify_event *)&event_buffer[i];

    /*
     * In cases when the mask is set to IN_IGNORED, it means that the watch descriptor was removed either explicitly or
     * implicitly. Since we are manually handling the removal of watch descriptors, we need to ignore these events to
     * prevent cache inconsistencies.
     *
     * So we only push the event onto the queue if the mask does not contain IN_IGNORED.
     */
    if (!(event->mask & IN_IGNORED))
    {
      _event_queue.push(FileEvent(event));
    }

    i += EVENT_SIZE + event->len;
  }
}

/**
 * Clears the watch descriptor cache and removes all watches.
 * @throws InotifyError if a watch descriptor could not be removed.
 */
void Inotify::clearCache()
{
  for (auto it = _wd_cache.begin(); it != _wd_cache.end();)
  {
    inotify_rm_watch(_inotify_fd, it->first);
    it = _wd_cache.erase(it);
  }
}

/**
 * The directory oldPathPrefix/oldName was renamed to newPathPrefix/newName.
 * Fix up cache entries for old_path_prefix/name and all of its subdirectories to reflect the change.
 * @param old_path_prefix The old path prefix to replace.
 * @param new_path_prefix The new path prefix to replace with.
 */
void Inotify::rewriteCachedPaths(const std::string &old_path_prefix, const std::string &new_path_prefix)
{
  for (auto &[wd, path] : _wd_cache)
  {
    if (path.string().rfind(old_path_prefix, 0) == 0)  // If the path starts with the old path prefix
    {
      std::string new_path_suffix = path.string().substr(old_path_prefix.size());  // Get the suffix
      path = new_path_prefix + std::move(new_path_suffix);                         // Replace the prefix
    }
  }
}

/* Zap watches and cache entries for directory 'old_path' and all of its
 * subdirectories.
 * @param old_path The path of the directory to remove watches for.
 * @throws InotifyError if the watch descriptor could not be removed.
 */
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
    inotify_rm_watch(_inotify_fd, wd);
  }
}

/* Processes an event that occurred against a directory.
 * @param event The event to process.
 */
void Inotify::processDirectoryEvent(const FileEvent &event)
{
  // The path of the directory that the event occurred in
  const std::filesystem::path &dir_path = _wd_cache.at(event.wd);
  // The path of the directory that the event is about
  const std::filesystem::path &full_path = dir_path / event.filename;

  if (event.mask & IN_DELETE)
  {
    _logger.logEvent("Deleted directory: %s", full_path.c_str());
    zapSubdirectories(full_path);
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

/* Processes an event that occurred against a file.
 * @param event The event to process.
 */
void Inotify::processFileEvent(const FileEvent &event)
{
  // The path of the directory that the event occurred in
  const std::filesystem::path &dir_path = _wd_cache.at(event.wd);
  // The path of the file that the event is about
  const std::filesystem::path &full_path = dir_path / event.filename;

  // A file was created or renamed into the watch directory
  if (event.mask & (IN_CREATE | IN_MOVED_TO)) _logger.logEvent("Created file: %s", full_path.c_str());
  // A file was deleted
  else if (event.mask & IN_DELETE)
    _logger.logEvent("Deleted file: %s", full_path.c_str());
  // A file was modified
  else if (event.mask & IN_MODIFY)
    _logger.logEvent("Modified file: %s", full_path.c_str());
  // A file was renamed or moved from the watch directory
  else if (event.mask & IN_MOVED_FROM)
  {
    // If no more events in the queue, we assume there will be no corresponding IN_MOVED_TO event
    if (_event_queue.empty())
    {
      _logger.logEvent("Moved file out of watch directory: %s", full_path.c_str());
    }
    else  // We assume that the next event is the corresponding IN_MOVED_TO event
    {
      const FileEvent next_event = _event_queue.front();
      _event_queue.pop();

      if (next_event.mask & IN_MOVED_TO && next_event.cookie == event.cookie)
      {
        // The path of the directory that the next event occurred in
        const std::filesystem::path &next_dir_path = _wd_cache.at(next_event.wd);
        // The path of the file that the next event is about
        const std::filesystem::path &next_full_path = next_dir_path / next_event.filename;

        if (dir_path == next_dir_path)  // If the directories are the same, it was a rename
          _logger.logEvent("Renamed file: %s -> %s", full_path.c_str(), next_full_path.c_str());
        else  // otherwise it was a move
          _logger.logEvent("Moved file: %s -> %s", full_path.c_str(), next_full_path.c_str());
      }
    }
  }
}
}  // namespace inotify
