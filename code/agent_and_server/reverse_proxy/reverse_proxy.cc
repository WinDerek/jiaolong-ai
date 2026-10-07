#include "reverse_proxy.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace reverse_proxy {

namespace {

// Set to a non-zero value by the signal handler when the user asks the process
// to terminate.
volatile std::sig_atomic_t g_stop_requested = 0;

// Signal handler installed for SIGINT and SIGTERM. It only records that a
// shutdown was requested; the guard loop notices the flag and exits.
void HandleStopSignal(int /*signal_number*/) { g_stop_requested = 1; }

}  // namespace

ForwardingProcess::ForwardingProcess(std::string command)
    : command_(std::move(command)) {}

bool ForwardingProcess::Start() {
  const pid_t child_pid = fork();
  if (child_pid < 0) {
    std::perror("Error: fork() failed");
    return false;
  }

  if (child_pid == 0) {
    // The child replaces itself with the forwarding command. It is placed in
    // its own session so that it does not receive the signals sent to this
    // program's process group. stdin is redirected from /dev/null because an
    // SSH forwarding command does not need any input.
    setsid();
    const int dev_null_fd = open("/dev/null", O_RDONLY);
    if (dev_null_fd >= 0) {
      dup2(dev_null_fd, STDIN_FILENO);
      close(dev_null_fd);
    }
    execl("/bin/sh", "sh", "-c", command_.c_str(), static_cast<char*>(nullptr));
    // execl only returns on failure.
    std::perror("Error: failed to run forwarding command");
    _exit(127);
  }

  pid_ = child_pid;
  return true;
}

bool ForwardingProcess::IsAlive() {
  if (pid_ <= 0) {
    return false;
  }

  int status = 0;
  const pid_t result = waitpid(pid_, &status, WNOHANG);
  if (result == pid_) {
    // The forwarding process exited; reap it and report it as dead so the
    // guard loop restarts it.
    pid_ = -1;
    return false;
  }
  if (result == 0) {
    // The process is still running.
    return true;
  }
  if (errno == EINTR) {
    // The wait was interrupted by a signal; assume the process is alive and
    // check again on the next iteration.
    return true;
  }
  // waitpid failed (for example ECHILD when the process is no longer a child).
  // Treat the forwarding as dead so it is restarted.
  pid_ = -1;
  return false;
}

void RunGuardLoop(const std::vector<std::string>& commands,
    int check_interval_seconds) {
  std::signal(SIGINT, HandleStopSignal);
  std::signal(SIGTERM, HandleStopSignal);

  std::vector<std::unique_ptr<ForwardingProcess>> forwardings;
  forwardings.reserve(commands.size());
  for (const std::string& command : commands) {
    forwardings.push_back(std::make_unique<ForwardingProcess>(command));
  }

  // Start every forwarding once before entering the guard loop.
  for (const auto& forwarding : forwardings) {
    if (forwarding->Start()) {
      std::cout << "[reverse_proxy] Started: " << forwarding->command()
                << std::endl;
    } else {
      std::cerr << "[reverse_proxy] Failed to start: " << forwarding->command()
                << std::endl;
    }
  }

  // The guard loop: run forever and bring any dead forwarding back to life.
  while (!g_stop_requested) {
    std::this_thread::sleep_for(std::chrono::seconds(check_interval_seconds));
    if (g_stop_requested) {
      break;
    }

    for (const auto& forwarding : forwardings) {
      if (forwarding->IsAlive()) {
        continue;
      }
      std::cout << "[reverse_proxy] Forwarding is down, restarting: "
                << forwarding->command() << std::endl;
      if (!forwarding->Start()) {
        std::cerr << "[reverse_proxy] Failed to restart: "
                  << forwarding->command() << std::endl;
      }
    }
  }

  std::cout << "[reverse_proxy] Shutdown requested, stopping." << std::endl;
}

}  // namespace reverse_proxy