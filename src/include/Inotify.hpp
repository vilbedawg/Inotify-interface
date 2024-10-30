#ifndef INOTIFY_HPP
#define INOTIFY_HPP

#include <sys/epoll.h>
#include <sys/inotify.h>
#include <atomic>
#include <filesystem>
#include <unordered_map>
#include <queue>
#include <vector>
#include "FileEvent.hpp"
#include "Logger.hpp"

#define MAX_EVENTS 4096 /* Max. number of events that can be read into the buffer */
#define NAME_MAX 16     /* Maximum number of bytes in filename */
#define EVENT_SIZE (sizeof(struct inotify_event)) /* Size of one inotify event */
#define EVENT_BUFFER_LEN (MAX_EVENTS * (EVENT_SIZE + NAME_MAX))

/* MAX_EPOLL_EVENTS is set to 1 since there exists only one eventbuffer */
#define MAX_EPOLL_EVENTS 1

namespace inotify {

class Inotify
{
 public:
  explicit Inotify(const std::filesystem::path& path);
  ~Inotify();

  void run();
  void stop();

 private:
  int addWatch(const std::filesystem::path& path);

  ssize_t readEventsIntoBuffer();
  void readEventsFromBuffer(ssize_t length);

  void processFileEvent(const FileEvent& event);
  void processDirectoryEvent(const FileEvent& event);
  void signalStop();
  void runOnce();

 private:
  int _inotify_fd; /* File descriptor for inotify */
  int _epoll_fd;   /* File descriptor for epoll */
  int _event_fd;   /* File descriptor for interrupt events */

  epoll_event _inotify_epoll_event;            /* For registering the inotify instance with epoll */
  epoll_event _stop_epoll_event;               /* For registering the stop event with epoll */
  epoll_event _epoll_events[MAX_EPOLL_EVENTS]; /* Array to store epoll events */

  std::unordered_map<int, std::filesystem::path>
      _wd_cache; /* Cache to store watch descriptors with their path */

  Logger _logger; /* For logging events */

  std::array<uint8_t, EVENT_BUFFER_LEN> _event_buffer; /* Buffer to store inotify events */
  std::queue<FileEvent> _event_queue;                  /* Queue to store inotify events */
  std::atomic<bool> _stopped;                          /* Flag to stop the inotify instance */
};

}  // namespace inotify

#endif  // INOTIFY_HPP
