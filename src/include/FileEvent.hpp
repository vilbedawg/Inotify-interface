#ifndef FILE_EVENT_HPP
#define FILE_EVENT_HPP

#include <sys/inotify.h>
#include <cstdint>
#include <chrono>
#include <string>

namespace inotify {

struct FileEvent {
  int wd;                                          /* Watch descriptor */
  uint32_t mask;                                   /* Watch mask */
  uint32_t cookie;                                 /* Cookie to synchronize two events */
  std::string filename;                            /* Name of the file */
  std::chrono::steady_clock::time_point timestamp; /* Timestamp of the event */

  FileEvent(inotify_event* event)
    : wd(event->wd)
    , mask(event->mask)
    , cookie(event->cookie)
    , filename(event->name)
    , timestamp(std::chrono::steady_clock::now())
  {
  }

  ~FileEvent() = default;
};

}  // namespace inotify

#endif  // FILE_EVENT_HPP
