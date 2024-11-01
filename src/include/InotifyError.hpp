#ifndef INOTIFYERROR_HPP
#define INOTIFYERROR_HPP

#include <stdexcept>
#include <string>
#include <cstring>
#include <cerrno>

namespace inotify {

struct InotifyError : public std::runtime_error {
  explicit InotifyError(const std::string& message) : std::runtime_error(message + ": " + std::strerror(errno)) {}
};

};  // namespace inotify

#endif  // INOTIFYERROR_HPP
