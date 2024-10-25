#include "../include/Notifier.hpp"

int main(int argc, char *argv[]) {
  inotify::Notifier notifier;
  notifier.watchDirectory(std::filesystem::current_path());
  notifier.unwatchDirectory(std::filesystem::current_path());
  return 0;
}
