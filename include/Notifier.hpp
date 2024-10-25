#ifndef NOTIFIER_HPP
#define NOTIFIER_HPP

#include <memory>
#include "Inotify.hpp"
#include <filesystem>

namespace inotify {
class Notifier
{
 public:
  Notifier();

  void run();
  void stop();

  void watchDirectory(const std::filesystem::path& path);
  void unwatchDirectory(const std::filesystem::path& path);

private:
  std::shared_ptr<Inotify> _inotify;
};
}  // namespace inotify

#endif  // NOTIFIER_HPP
