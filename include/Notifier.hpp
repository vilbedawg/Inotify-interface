#ifndef NOTIFIER_HPP
#define NOTIFIER_HPP

#include <sys/inotify.h>
#include <memory>
#include "Inotify.hpp"
#include <filesystem>

namespace inotify {

class Notifier
{
 public:
  Notifier(std::filesystem::path&& path);
  void run();
  void stop();

 private:
  std::shared_ptr<Inotify> _inotify;
  void watchDirectory(const std::filesystem::path& path);
  void unwatchDirectory(int wd);
  void processEvent(inotify_event* node);
  void notifyEvent(inotify_event* node);
  // std::string getMaskString(uint32_t mask);
  // std::string getFormattedTime(const std::chrono::time_point<std::chrono::system_clock>& time);
};
}  // namespace inotify

#endif  // NOTIFIER_HPP
