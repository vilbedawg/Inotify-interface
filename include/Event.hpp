#ifndef EVENT_HPP

#include <filesystem>
namespace inotify {
class Event
{
 public:
  Event(int watch_descriptor,
      uint32_t mask,
      const std::filesystem::path &path,
      const std::chrono::system_clock::time_point &event_time);

  ~Event();

 public:
  int watch_descriptor;
  uint32_t mask;
  std::filesystem::path path;
  std::chrono::system_clock::time_point event_time;
};
}  // namespace inotify

#endif  // EVENT_HPP
