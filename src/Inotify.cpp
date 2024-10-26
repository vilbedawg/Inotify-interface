#include "../include/Inotify.hpp"
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <sys/inotify.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace inotify {

Inotify::Inotify() : _event_buffer(EVENT_BUFFER_LEN, 0)
{
  std::cout << "Inotify constructor" << std::endl;
  std::stringstream error_stream;

  // Initialize the inotify instance
  _inotify_fd = inotify_init();
  if (_inotify_fd < 0)
  {
    error_stream << "Failed to initialize inotify: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }

  // Initialize the epoll instance
  _epoll_fd = epoll_create1(0);
  if (_epoll_fd < 0)
  {
    error_stream << "Failed to create epoll instance: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }

  // Register inotify file descriptor with epoll
  _inotify_epoll_event.events = EPOLLIN;
  _inotify_epoll_event.data.fd = _inotify_fd;
  if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _inotify_fd, &_inotify_epoll_event) == -1)
  {
    close(_inotify_fd);
    close(_epoll_fd);
    error_stream << "Failed to add inotify file descriptor to epoll: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }
}

Inotify::~Inotify()
{
  epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, _inotify_fd, 0);
  close(_inotify_fd);
  close(_epoll_fd);
}

void Inotify::watchDirectory(const std::filesystem::path &path)
{
  std::stringstream error_stream;

  if (!checkWatchDirectory(path)) return;

  int watch_descriptor =
      inotify_add_watch(_inotify_fd, path.c_str(), IN_CREATE | IN_DELETE | IN_MODIFY);

  if (watch_descriptor == -1)
  {
    error_stream << "Failed to watch directory: " << strerror(errno);
    throw std::runtime_error("Failed to watch directory: " + path.string());
  }

  std::cout << "Watching " << path << std::endl;
  _watch_descriptors_map.insert(std::make_pair(watch_descriptor, path));
}

void Inotify::unwatchDirectory(const std::filesystem::path &path)
{
  std::stringstream error_stream;

  // find the watch descriptor from the map with the path
  int watch_descriptor = -1;
  for (auto it = _watch_descriptors_map.begin(); it != _watch_descriptors_map.end(); ++it)
  {
    if (it->second == path)
    {
      watch_descriptor = it->first;
      break;
    }
  }

  if (watch_descriptor == -1)
  {
    error_stream << "Failed to unwatch directory: " << path << " is not being watched";
    throw std::invalid_argument(error_stream.str());
  }

  int result = inotify_rm_watch(_inotify_fd, watch_descriptor);
  if (result == -1)
  {
    error_stream << "Failed to unwatch directory: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }

  std::cout << "No longer watching " << path << std::endl;
}

bool Inotify::checkWatchDirectory(const std::filesystem::path &path)
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

  return true;
}

std::optional<Event> Inotify::readNextEvent()
{
  while (_event_queue.empty())
  {
    ssize_t length = readEventsIntoBuffer();
    readEventsFromBuffer(length);
  }

  Event nextEvent = _event_queue.front();
  _event_queue.pop();
  return nextEvent;
}

ssize_t Inotify::readEventsIntoBuffer()
{
  ssize_t no_of_events = 0;
  const int timeout = -1;
  int triggered_events = epoll_wait(_epoll_fd, _epoll_events, MAX_EVENTS, timeout);

  if (triggered_events == -1) return no_of_events;

  for (int i = 0; i < triggered_events; ++i)
  {
    if (_epoll_events[i].data.fd != _inotify_fd) continue;

    no_of_events =
        read(_inotify_fd, _event_buffer.data(), _event_buffer.size());  // Reads events into buffer

    if (no_of_events == -1)
    {
      std::stringstream error_stream;
      error_stream << "Failed to read events: " << strerror(errno);
      throw std::runtime_error(error_stream.str());
    }
  }

  return no_of_events;
}

void Inotify::readEventsFromBuffer(ssize_t length)
{
  uint8_t *event_buffer = _event_buffer.data();
  struct inotify_event *event;

  for (ssize_t i = 0; i < length; i += EVENT_SIZE + event->len)
  {
    event = (struct inotify_event *)&event_buffer[i];
    std::string path = _watch_descriptors_map[event->wd];
    std::string filename = event->len > 0 ? event->name : "";

    Event e(event->wd, event->mask, path + "/" + filename, std::chrono::steady_clock::now());
    _event_queue.push(e);
  }
}

}  // namespace inotify
