#ifndef INOTIFY_HPP
#define INOTIFY_HPP

#include <sys/epoll.h>
#include <sys/inotify.h>
#include <atomic>
#include <filesystem>
#include <queue>
#include <vector>

#define MAX_EVENTS 4096                            // Max. number of events to process at one go
#define NAME_MAX 16                                // Maximum number of bytes in filename
#define EVENT_SIZE (sizeof(struct inotify_event))  // size of one event
#define EVENT_BUFFER_LEN (MAX_EVENTS * (EVENT_SIZE + NAME_MAX))

#define MAX_EPOLL_EVENTS 1  // Only 1 event buffer exists

namespace inotify {

static const uint32_t NOTIFY_FLAGS =
    IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_ONLYDIR;

class Inotify
{
 public:
  Inotify();
  ~Inotify();

  int addWatch(const std::filesystem::path &path);
  void removeWatch(int wd);
  inotify_event *readNextEvent();

  void stop();
  bool isStopped() const;

 private:
  ssize_t readEventsIntoBuffer();
  void readEventsFromBuffer(ssize_t length);

 private:
  int _inotify_fd;  // File descriptor for inotify
  int _epoll_fd;    // File descriptor for epoll
  int _event_fd;    // File descriptor for eventfd

  epoll_event _inotify_epoll_event;             // Epoll event for inotify
  epoll_event _stop_epoll_event;                // Epoll event for interrupting epolls
  epoll_event _epoll_events[MAX_EPOLL_EVENTS];  // Array of epoll events

  std::vector<uint8_t> _event_buffer;        // Buffer to store inotify events
  std::queue<inotify_event *> _event_queue;  // Queue to store inotify events
  std::atomic<bool> _stopped;                // Flag to stop the inotify instance
};

}  // namespace inotify

#endif  // INOTIFY_HPP
