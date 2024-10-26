#include "../include/Event.hpp"

inotify::Event::Event(int watch_descriptor,
    uint32_t mask,
    const std::filesystem::path& path,
    const std::chrono::steady_clock::time_point& event_time)
  : watch_descriptor(watch_descriptor), mask(mask), path(path), timestamp(formatTime(event_time))
{
}

inotify::Event::~Event() {}

std::string inotify::Event::formatTime(
    const std::chrono::steady_clock::time_point& event_time) const
{
  auto time = std::chrono::system_clock::now() + (event_time - std::chrono::steady_clock::now());
  std::time_t time_t_format = std::chrono::system_clock::to_time_t(time);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&time_t_format), "%Y-%m-%d %H:%M:%S");
  return ss.str();
}

