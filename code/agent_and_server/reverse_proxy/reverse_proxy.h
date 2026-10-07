#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

namespace reverse_proxy {

// Owns a single SSH forwarding process started from a shell command and knows
// how to tell whether that process is still running.
class ForwardingProcess {
 public:
  explicit ForwardingProcess(std::string command);

  ForwardingProcess(const ForwardingProcess&) = delete;
  ForwardingProcess& operator=(const ForwardingProcess&) = delete;

  // Starts the command as a child process. Returns true when the process was
  // spawned, false when it could not be started.
  bool Start();

  // Returns true when the forwarding process is still alive. An already-exited
  // child is reaped here.
  bool IsAlive();

  const std::string& command() const { return command_; }

 private:
  std::string command_;
  pid_t pid_ = -1;
};

// Runs forever (a guard loop): every `check_interval_seconds` it verifies that
// every configured forwarding is still alive and restarts the ones that died.
// The loop stops when the process receives SIGINT or SIGTERM.
void RunGuardLoop(const std::vector<std::string>& commands,
    int check_interval_seconds);

}  // namespace reverse_proxy