#ifndef INOTIFY_HPP
#define INOTIFY_HPP

#include <sys/epoll.h>
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
#define EVENT_BUFFER_LEN (MAX_EVENTS * (EVENT_SIZE + NAME_MAX))
#define MAX_EPOLL_EVENTS 1  // Only 1 event buffer exists

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
  ssize_t readEventsIntoBuffer();
  void readEventsFromBuffer(ssize_t length);

  bool checkWatchDirectory(const std::filesystem::path &path);

 private:
  int _inotify_fd;
  int _epoll_fd;
  std::map<int, std::string> _watch_descriptors_map;
  std::vector<uint8_t> _event_buffer;
  std::queue<Event> _event_queue;

  epoll_event _inotify_epoll_event;
  epoll_event _epoll_events[MAX_EPOLL_EVENTS];
};
}  // namespace inotify

#endif  // INOTIFY_HPP
