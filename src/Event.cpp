#include "../include/Event.hpp"

inotify::Event::Event(int watch_descriptor,
    uint32_t mask,
    const std::filesystem::path& path,
    const std::chrono::steady_clock::time_point & event_time)
    : watch_descriptor(watch_descriptor),
      mask(mask),
      path(path),
      event_time(event_time)
{
}

inotify::Event::~Event() {}
