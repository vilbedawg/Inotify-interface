#include "../include/Notifier.hpp"
#include <iostream>
#include <thread>
#include <csignal>

// Setup global interrupt handler
static bool running = true;
static bool had_error = false;
static auto signalHandler = [](int signal) { running = false; };

int main(int argc, char *argv[])
{
  std::signal(SIGINT, signalHandler);  // Capture interrupt signal (Ctrl+c)
  std::cout << "Press Ctrl+c to stop the program" << std::endl;

  // Initialize the notifier and watch the specified directory.
  inotify::Notifier notifier{argc <= 1 ? std::filesystem::current_path() : argv[1]};

  // Start the notifier in a separate thread context.
  std::thread thread([&]() {
    try
    {
      notifier.run();
    } catch (std::exception &e)
    {
      std::cerr << "Notifier had an exception: " << e.what() << std::endl;
      running = false;
      had_error = true;
    }
  });

  // Keep the main thread alive until the notifier is stopped.
  while (running) std::this_thread::sleep_for(std::chrono::milliseconds(500));

  notifier.stop();
  thread.join();

  if (had_error) return EXIT_FAILURE;

  return EXIT_SUCCESS;
}
