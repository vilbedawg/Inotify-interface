#include "include/Logger.hpp"

#include <chrono>
#include <cstdarg>
#include <iomanip>
#include <iostream>

namespace inotify {

void Logger::logEvent(const char* format, ...)
{
  std::cout << '[' << getTimestamp() << "] ";
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  std::cout << std::endl;
}

std::string Logger::getTimestamp() const
{
  // Get the current time as system time
  auto now = std::chrono::system_clock::now();
  auto now_t = std::chrono::system_clock::to_time_t(now);

  // Convert to local time
  std::tm now_tm = *std::localtime(&now_t);

  // Format the time as YYYY-MM-DD HH:MM:SS
  std::ostringstream oss;
  oss << std::put_time(&now_tm, "%d-%m-%Y %H:%M:%S");

  return oss.str();
}

}  // namespace inotify
