#include "../include/Inotify.hpp"
#include <iostream>
#include <thread>
#include <csignal>
#include <memory>
#include <filesystem>

// Global state variables for program status
namespace {
bool running = true;
bool had_error = false;
}  // namespace

// Signal handler to gracefully stop the program
void signalHandler(int signal) { running = false; }

int main(int argc, char* argv[])
{
  if (argc < 1)
  {
    std::cerr << "Usage: " << argv[0] << " [path]" << std::endl;
    return EXIT_FAILURE;
  };

  std::signal(SIGINT, signalHandler);  // Setup interrupt signal handler
  std::cout << "Press Ctrl+C to stop the program." << std::endl;

  const auto path = std::filesystem::path(argv[1]);
  auto inotify = std::make_shared<inotify::Inotify>(std::move(path));

  std::thread thread([inotify]() {
    try
    {
      inotify->run();
    } catch (std::exception& e)
    {
      running = false;
      had_error = true;
    }
  });

  // Keep the main thread alive until the signal is received
  while (running)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  inotify->stop();
  thread.join();

  return had_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
