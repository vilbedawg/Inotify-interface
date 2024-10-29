#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>

namespace inotify {

class Logger
{
 public:
  void logEvent(const char *format, ...);

 private:
  std::string getTimestamp() const;
};

}  // namespace inotify

#endif  // LOGGER_HPP
