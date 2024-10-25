#include "../include/Notifier.hpp"
#include <iostream>

namespace inotify {

Notifier::Notifier() : _inotify(std::make_shared<Inotify>()) {}

void Notifier::watchDirectory(const std::filesystem::path& path)
{
  _inotify->watchDirectory(path);  // Delegate the call to the Inotify object
}

void Notifier::unwatchDirectory(const std::filesystem::path& path)
{
  _inotify->unwatchDirectory(path); // Delegate the call to the Inotify object
}

void Notifier::run()
{
  auto event = _inotify->readNextEvent();
  std::cout << "Event occurred" << std::endl;
}

}  // namespace inotify
