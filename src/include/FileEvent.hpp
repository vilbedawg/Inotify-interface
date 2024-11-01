#ifndef FILE_EVENT_HPP
#define FILE_EVENT_HPP

#include <sys/inotify.h>
#include <cstdint>
#include <string>

namespace inotify {

struct FileEvent {
  int wd;               /* Watch descriptor */
  uint32_t mask;        /* Watch mask */
  uint32_t cookie;      /* Cookie to synchronize two events */
  std::string filename; /* Name of the file */

  FileEvent(const inotify_event* const event);
  ~FileEvent();
};

}  // namespace inotify

#endif  // FILE_EVENT_HPP
