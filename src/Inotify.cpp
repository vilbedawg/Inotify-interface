#include "../include/Inotify.hpp"
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <sys/inotify.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace inotify {

Inotify::Inotify(const std::filesystem::path &path) : _event_buffer(EVENT_BUFFER_LEN, 0)
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
  addWatch(path);
  for (const auto &entry : std::filesystem::recursive_directory_iterator(path))
  {
    if (entry.is_directory())
    {
      addWatch(entry.path().string());
    }
  }

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

void Inotify::stop()
{
  _stopped = true;

  // Sends a signal to the eventfd to interrupt epoll_wait
  uint64_t buffer = 1;
  if (write(_event_fd, &buffer, sizeof(buffer)) == -1)
  {
    throw std::runtime_error("Failed to signal eventfd for stop");
  }
}

int Inotify::addWatch(const std::string &pathname)
{
  std::stringstream error_stream;
  if (!std::filesystem::exists(pathname))
  {
    error_stream << "Specified path does not exist: " << pathname;
    throw std::invalid_argument(error_stream.str());
  }

  if (!std::filesystem::is_directory(pathname))
  {
    error_stream << "Specified path is not a directory: " << pathname;
    throw std::invalid_argument(error_stream.str());
  }

  const int flags = IN_MODIFY | IN_CREATE | IN_MOVE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF;

  int wd = inotify_add_watch(_inotify_fd, pathname.c_str(), flags);
  if (wd == -1)
  {
    error_stream << "Failed to watch directory: " << strerror(errno);
    throw std::runtime_error("Failed to watch directory: " + pathname);
  }

  _wd_cache.insert({wd, pathname});

  return wd;
}

void Inotify::removeWatch(int wd)
{
  if (inotify_rm_watch(_inotify_fd, wd) == -1)
  {
    std::stringstream error_stream;
    error_stream << "Failed to unwatch directory: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }
}

void Inotify::run()
{
  while (!_stopped)
  {
    ssize_t length = readEventsIntoBuffer();
    if (length > 0) readEventsFromBuffer(length);
  }
}

ssize_t Inotify::readEventsIntoBuffer()
{
  ssize_t length = 0;
  const int timeout = -1;
  int triggered_events = epoll_wait(_epoll_fd, _epoll_events, MAX_EVENTS, timeout);

  if (triggered_events == -1) return length;

  for (int i = 0; i < triggered_events; ++i)
  {
    if (_epoll_events[i].data.fd == _event_fd) break;

    if (_epoll_events[i].data.fd == _inotify_fd)
    {
      length = read(_inotify_fd, _event_buffer.data(), _event_buffer.size());

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
  ssize_t i = 0;
  while (i < length)
  {
    const auto event = (struct inotify_event *)&event_buffer[i];

    if (event->mask & IN_IGNORED)
    {
      /* Watch was removed explicitly or automatically
       * clear the corresponding item from the cache. */
      _wd_cache.erase(event->wd);
      i += EVENT_SIZE + event->len;
      continue;
    }

    std::string full_path = _wd_cache.at(event->wd) / std::string(event->name);

    if ((event->mask & IN_ISDIR) && (event->mask & (IN_CREATE | IN_MOVED_TO)))
    {
      /* A new subdirectory was created, or a subdirectory was renamed into
       * the tree; create watches for it, and all of its subdirectories. */
      _logger.logEvent("Created directory: %s", full_path.c_str());

      addWatch(full_path);
      for (const auto &entry : std::filesystem::recursive_directory_iterator(full_path))
      {
        if (entry.is_directory())
        {
          addWatch(entry.path().string());
        }
      }
    }
    else if (event->mask & IN_DELETE_SELF)
    {
      /* A directory was deleted. Remove the corresponding item from the cache. */
      _logger.logEvent("Deleted directory: %s", full_path.c_str());
      _wd_cache.erase(event->wd);
    }
    else if (event->mask & IN_MOVED_FROM)
    {
      struct inotify_event *next_event =
          (struct inotify_event *)&event_buffer[i + EVENT_SIZE + event->len];

      if (next_event->len < length && (next_event->mask & IN_MOVED_TO) &&
          (next_event->cookie == event->cookie))
      {
        std::string new_path = _wd_cache.at(next_event->wd) / std::string(next_event->name);
        if (event->mask & IN_ISDIR)
        {
          _logger.logEvent("Renamed directory %s ==> %s", full_path.c_str(), new_path.c_str());
        }
        else
        {
          _logger.logEvent("Renamed file %s ==> %s", full_path.c_str(), new_path.c_str());
        }

        i += EVENT_SIZE + next_event->len;
      }
      else
      {
        _logger.logEvent("Moved out of watch directory: %s", full_path.c_str());
      }
    }
    else if (event->mask & IN_MODIFY)
    {
      _logger.logEvent("Modified file: %s", full_path.c_str());
    }
    else if (event->mask & IN_CREATE)
    {
      _logger.logEvent("Created file: %s", full_path.c_str());
    }
    else if (event->mask & IN_DELETE)
    {
      if (!(event->mask & IN_ISDIR))
      {
        _logger.logEvent("Deleted file: %s", full_path.c_str());
      }
      /* ignore delete events for directories since they are handled by IN_DELETE_SELF */
    }

    i += EVENT_SIZE + event->len;
  }
}

}  // namespace inotify
