#ifndef INOTIFY_HPP
#define INOTIFY_HPP

#include <sys/inotify.h>
#include "Event.hpp"
#include <map>
#include <optional>
#include <queue>
#include <string>
#include <filesystem>
#include <vector>

#define MAX_EVENTS 4096                            // Max. number of events to process at one go
#define NAME_MAX 16                                // Maximum number of bytes in filename
#define EVENT_SIZE (sizeof(struct inotify_event))  // size of one event

namespace inotify {

class Inotify
{
 public:
  Inotify();
  ~Inotify();

  void watchDirectory(const std::filesystem::path &path);
  void unwatchDirectory(const std::filesystem::path &path);
  std::optional<Event> readNextEvent();
  void stop();
  void hasStopped();

private:
  ssize_t readEventsToBuffer();

 private:
  int _file_descriptor;
  std::map<int, std::string> _watch_descriptors_map;
  std::vector<uint8_t> _eventBuffer;
  std::queue<Event> _eventQueue;
};
}  // namespace inotify

#endif  // INOTIFY_HPP
