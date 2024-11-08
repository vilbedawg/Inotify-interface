#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>

#include "include/Inotify.hpp"

// Global state variables
bool running = true;
bool had_error = false;

// Signal handler to gracefully stop the program
void signalHandler(int) { running = false; }

// Check if a directory exists and is valid
bool isValidDirectory(const std::filesystem::path& path)
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

// Parse and validate command-line arguments
bool parseArguments(int argc, char* argv[], std::filesystem::path& path, std::vector<std::string>& ignored_dirs)
{
  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " [path] [ignored_dirs...]" << std::endl;
    return false;
  }

  path = std::filesystem::path(argv[1]);
  if (!isValidDirectory(path)) return false;

  for (int i = 2; i < argc; ++i)
  {
    ignored_dirs.push_back(argv[i]);
  }
  return true;
}

// Display information about the monitored directory and ignored directories
void displayWatchInfo(const std::filesystem::path& path, const std::vector<std::string>& ignored_dirs)
{
  std::cout << "Press Ctrl+C to stop the program." << std::endl;
  std::cout << "Watching directory: " << path << std::endl;
  std::cout << "Ignored directories: ";
  for (size_t i = 0; i < ignored_dirs.size(); ++i)
  {
    std::cout << ignored_dirs[i];
    if (i < ignored_dirs.size() - 1) std::cout << ", ";
  }
  std::cout << std::endl;
}

// Run the Inotify instance in a separate thread context
void runInotify(inotify::Inotify& inotify_instance)
{
  try
  {
    inotify_instance.run();
  } catch (const std::exception& e)
  {
    std::cerr << "Unexpected error: " << e.what() << std::endl;
    had_error = true;
  }
  running = false;
}

int main(int argc, char* argv[])
{
  std::filesystem::path path;
  std::vector<std::string> ignored_dirs;

  if (!parseArguments(argc, argv, path, ignored_dirs)) return EXIT_FAILURE;

  inotify::Inotify inotify_instance(path, ignored_dirs);

  std::signal(SIGINT, signalHandler);  // Setup interrupt signal handler

  displayWatchInfo(path, ignored_dirs);

  std::thread watcher_thread(runInotify, std::ref(inotify_instance));

  // Keep main thread alive until interrupted
  while (running) std::this_thread::sleep_for(std::chrono::milliseconds(500));

  inotify_instance.stop();
  watcher_thread.join();

  std::cout << "Bye!" << std::endl;
  return had_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
