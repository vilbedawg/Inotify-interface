#include "../include/Notifier.hpp"
#include <cassert>
#include <iostream>

namespace inotify {

Notifier::Notifier(std::filesystem::path&& path) : _inotify(std::make_shared<Inotify>())
{
  watchDirectory(path);
  std::cout << "Watching directory: " << path.string() << std::endl;
}

void Notifier::watchDirectory(const std::filesystem::path& path)
{
  int wd = _inotify->addWatch(path);

  for (const auto& entry : std::filesystem::recursive_directory_iterator(path))
  {
    if (entry.is_directory())
    {
      watchDirectory(entry.path());
    }
  }
}

void Notifier::run()
{
  while (!_inotify->isStopped())
  {
    inotify_event* event = _inotify->readNextEvent();

    if (event == nullptr) break;  // Event doesn't have value if the notifier is stopped

    processEvent(event);
    notifyEvent(event);
  }
}

void Notifier::stop()
{
  std::cout << "Stopping..." << std::endl;
  _inotify->stop();
}

void Notifier::processEvent(inotify_event* event)
{
  std::cout << "Event: " << event->name << std::endl;
}

void Notifier::notifyEvent(inotify_event* event) {}

}  // namespace inotify
