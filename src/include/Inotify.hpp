#ifndef INOTIFY_HPP
#define INOTIFY_HPP

#include <sys/epoll.h>
#include <sys/inotify.h>

#include <atomic>
#include <filesystem>
#include <queue>
#include <unordered_map>
#include <vector>

#include "FileEvent.hpp"
#include "Logger.hpp"

#define MAX_EVENTS 4096                           /* Max. number of events that can be read into the buffer */
#define NAME_MAX 16                               /* Maximum number of bytes in filename */
#define EVENT_SIZE (sizeof(struct inotify_event)) /* Size of one inotify event */
#define EVENT_BUFFER_LEN (MAX_EVENTS * (EVENT_SIZE + NAME_MAX))

/* MAX_EPOLL_EVENTS is set to 1 since there exists only one eventbuffer */
#define MAX_EPOLL_EVENTS 1

namespace inotify {

class Inotify
{
 public:
  Inotify(const std::filesystem::path& path, const std::vector<std::string>& ignored_dirs);
  ~Inotify();

  void run();  /* Starts the event-loop */
  void stop(); /* Stops the event-loop */

 private:
  void initialize();         /* Initialize inotify and epoll */
  void terminate() noexcept; /* Gracefully terminate the inotify instance */
  void runOnce();            /* Run a single iteration of the event-processing loop */
  void reinitialize();       /* Reinitialize inotify and epoll, and try to rewrite the cache from scracth */

  /* Directory and path managment */
  bool watchDirectory(
      const std::filesystem::path& path);             /* Add a directory and all of its subdirectories to be watched */
  int addWatch(const std::filesystem::path& path);    /* Register a directory path with inotify */
  bool isIgnored(const std::string& path_name) const; /* Check if a directory is in the ignore list */
  int zapSubdirectories(const std::filesystem::path&
          old_path); /* Remove all subdirectories from the watch descriptor cache under the given path */

  /* Cache mangament */
  void rewriteCachedPaths(const std::string& old_path_prefix,
      const std::string& new_path_prefix);       /* Update cached paths */
  int findWd(const std::filesystem::path& path); /* Find the watch descriptor for the given path */

  /* Event handling */
  ssize_t readEventsIntoBuffer();            /* Reads inotify events into the _event_buffer */
  void readEventsFromBuffer(ssize_t length); /* Reads inotify events from the _event_buffer to the _event_queue */

  /* Event processing */
  void processFileEvent(const FileEvent& event);      /* Handle file related events */
  void processDirectoryEvent(const FileEvent& event); /* Handle directory related events */

 private:
  const std::filesystem::path _root;                        /* Root path to watch */
  const std::vector<std::string> _ignored_dirs;             /* Directories to ignore */
  int _inotify_fd;                                          /* File descriptor for inotify */
  int _epoll_fd;                                            /* File descriptor for epoll */
  int _event_fd;                                            /* File descriptor for interrupt events */
  epoll_event _inotify_epoll_event;                         /* For registering the inotify instance with epoll */
  epoll_event _stop_epoll_event;                            /* For registering the stop event with epoll */
  epoll_event _epoll_events[MAX_EPOLL_EVENTS];              /* Array to store epoll events */
  std::unordered_map<int, std::filesystem::path> _wd_cache; /* Cache to store watch descriptors with their path */
  std::array<uint8_t, EVENT_BUFFER_LEN> _event_buffer;      /* Buffer to store inotify events */
  std::queue<FileEvent> _event_queue;                       /* Queue to store inotify events */
  std::atomic<bool> _stopped;                               /* Flag to stop the inotify instance */
  Logger _logger;                                           /* For logging events */
};

}  // namespace inotify

#endif  // INOTIFY_HPP
