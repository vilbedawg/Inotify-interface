#ifndef INOTIFYERROR_HPP
#define INOTIFYERROR_HPP

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace inotify {

struct InotifyError : public std::runtime_error {
  explicit InotifyError(const std::string& message) : std::runtime_error(message + ": " + std::strerror(errno)) {}
};

};  // namespace inotify

#endif  // INOTIFYERROR_HPP
