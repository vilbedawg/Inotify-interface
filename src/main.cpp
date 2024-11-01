#include "include/Inotify.hpp"
#include <iostream>
#include <thread>
#include <csignal>
#include <memory>
#include <filesystem>

// Global state variables for program status
bool running = true;
bool had_error = false;

// Signal handler to gracefully stop the program
static void signalHandler(int signal) { running = false; }

bool checkDirectory(const std::filesystem::path& path)
{
  if (!std::filesystem::exists(path))
  {
    std::cerr << "Specified path does not exist: " << path << std::endl;
    return false;
  }

  if (!std::filesystem::is_directory(path))
  {
    std::cerr << "Specified path is not a directory: " << path << std::endl;
    return false;
  }

  return true;
}

int main(int argc, char* argv[])
{
  if (argc < 1)
  {
    std::cerr << "Usage: " << argv[0] << " [path] [ignored_dirs...]" << std::endl;
    return EXIT_FAILURE;
  };

  std::signal(SIGINT, signalHandler);  // Setup interrupt signal handler
  std::cout << "Press Ctrl+C to stop the program." << std::endl;

  const auto path = std::filesystem::path(argv[1]);
  if (!checkDirectory(path)) return EXIT_FAILURE;

  std::vector<std::string> ignored_dirs;
  if (argc > 2)
  {
    for (int i = 2; i < argc; ++i)
    {
      if (!checkDirectory(argv[i])) return EXIT_FAILURE;

      ignored_dirs.push_back(argv[i]);
    }
  }

  ignored_dirs.push_back(".git");  // Always ignore the .git directory

  inotify::Inotify inotify(path, ignored_dirs);

  std::thread thread([&inotify]() {
    try
    {
      inotify.run();
    } catch (std::exception& e)
    {
      std::cout << "Unexpected error: " << e.what() << std::endl;
      had_error = true;
    }

    running = false;
  });

  // Keep the main thread alive until the signal is received
  while (running)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  inotify.stop();
  thread.join();

  return had_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
