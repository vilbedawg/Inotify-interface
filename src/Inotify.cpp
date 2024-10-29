#include "../include/Inotify.hpp"
#include <cstring>
#include <filesystem>
#include <sstream>
#include <sys/inotify.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace inotify {

Inotify::Inotify() : _event_buffer(EVENT_BUFFER_LEN, 0)
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

bool Inotify::isStopped() const { return _stopped; }

int Inotify::addWatch(const std::filesystem::path &path)
{
  if (!std::filesystem::exists(path))
  {
    throw std::invalid_argument(
        "Failed to check watch directory: " + path.string() + " does not exist");
  }

  if (!std::filesystem::is_directory(path))
  {
    throw std::invalid_argument(
        "Failed to check watch directory: " + path.string() + " is not a directory");
  }

  int wd = inotify_add_watch(_inotify_fd, path.c_str(), NOTIFY_FLAGS);
  if (wd == -1)
  {
    std::stringstream error_stream;
    error_stream << "Failed to watch directory: " << strerror(errno);
    throw std::runtime_error("Failed to watch directory: " + path.string());
  }

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

inotify_event *Inotify::readNextEvent()
{
  while (_event_queue.empty() && !_stopped)
  {
    ssize_t length = readEventsIntoBuffer();
    if (length > 0) readEventsFromBuffer(length);
  }

  if (_stopped) return nullptr;

  inotify_event *nextEvent = _event_queue.front();
  _event_queue.pop();
  return nextEvent;
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
    _event_queue.push(event);
    i += EVENT_SIZE + event->len;
  }
}

}  // namespace inotify
