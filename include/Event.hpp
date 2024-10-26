#ifndef EVENT_HPP

#include <filesystem>
#include <sys/inotify.h>

namespace inotify {
class Event
{
 public:
  Event(int watch_descriptor,
      uint32_t mask,
      const std::filesystem::path &path,
      const std::chrono::steady_clock::time_point &event_time);

  ~Event();

  std::string getEventType() const;
  std::string getEventTime() const;

private:
  std::string formatTime(const std::chrono::steady_clock::time_point &event_time) const;

 public:
  int watch_descriptor;
  uint32_t mask;
  const std::filesystem::path path;
  const std::chrono::steady_clock::time_point event_time;
};
}  // namespace inotify

#endif  // EVENT_HPP
