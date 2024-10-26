#include "../include/Inotify.hpp"
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <sys/inotify.h>
#include <unistd.h>

namespace inotify {

Inotify::Inotify()
: _eventBuffer(MAX_EVENTS * (EVENT_SIZE + NAME_MAX))
{
  std::cout << "Inotify constructor" << std::endl;
  _file_descriptor = inotify_init();
  if (_file_descriptor < 0)
  {
    std::stringstream error_stream;
    error_stream << "Failed to initialize inotify: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }
}

Inotify::~Inotify()
{
  std::cout << "Inotify destructor" << std::endl;
  close(_file_descriptor);
}

void Inotify::watchDirectory(const std::filesystem::path &path)
{
  std::stringstream error_stream;

  /* Check if the path exists */
  if (!std::filesystem::exists(path))
  {
    error_stream << "Failed to watch directory: " << path << " does not exist";
    throw std::runtime_error(error_stream.str());
  }

  /* Check if the path is a directory */
  if (!std::filesystem::is_directory(path))
  {
    error_stream << "Failed to watch directory: " << path << " is not a directory";
    throw std::runtime_error(error_stream.str());
  }

  int watch_descriptor =
      inotify_add_watch(_file_descriptor, path.c_str(), IN_CREATE | IN_DELETE | IN_MODIFY);

  if (watch_descriptor == -1)
  {
    error_stream << "Failed to watch directory: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
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
    throw std::runtime_error(error_stream.str());
  }

  int result = inotify_rm_watch(_file_descriptor, watch_descriptor);
  if (result == -1)
  {
    error_stream << "Failed to unwatch directory: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }

  std::cout << "No longer watching " << path << std::endl;
}

std::optional<Event> Inotify::readNextEvent()
{
  std::stringstream error_stream;

  ssize_t length = readEventsToBuffer();
  ssize_t i = 0;

  while (i < length)
  {
    const auto *event = (struct inotify_event *)&_eventBuffer[i];
    std::string path = _watch_descriptors_map[event->wd];
    std::string filename = event->len > 0 ? event->name : "";
  
    Event e(event->wd, event->mask, path + "/" + filename, std::chrono::steady_clock::now());
    _eventQueue.push(e);
  
    i += EVENT_SIZE + event->len;
  }
  //
  // return _eventQueue.front();
  return std::nullopt;
}

ssize_t Inotify::readEventsToBuffer()
{
  ssize_t length = read(_file_descriptor, _eventBuffer.data(), _eventBuffer.size());
  if (length == -1)
  {
    std::stringstream error_stream;
    error_stream << "Failed to read events: " << strerror(errno);
    throw std::runtime_error(error_stream.str());
  }

  return length;
}

}  // namespace inotify