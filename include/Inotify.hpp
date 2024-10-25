#ifndef INOTIFY_HPP
#define INOTIFY_HPP

#include <sys/inotify.h>
#include <map>
#include <string>
#include <filesystem>

#define MAX_EVENTS 4096                            // Max. number of events to process at one go
#define EVENT_SIZE (sizeof(struct inotify_event))  // size of one event

namespace inotify {

class Inotify
{
 public:
  Inotify();
  ~Inotify();

  void watchDirectory(const std::filesystem::path &path);
  void unwatchDirectory(const std::filesystem::path &path);
  void stop();
  void hasStopped();

 private:
  int _file_descriptor;
  std::map<int, std::string> _watch_descriptors_map;
};
}  // namespace inotify

#endif  // INOTIFY_HPP
