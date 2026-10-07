#include "util/command_runner.h"

#include <cstdio>
#include <string>
#include <sys/wait.h>

namespace jiaolong {

std::pair<int, std::string> RunCommandAndCaptureOutput(
    const std::string& command) {
  // Redirect stderr to stdout so the combined output of the command is
  // captured.
  const std::string command_with_stderr = command + " 2>&1";

  FILE* pipe = popen(command_with_stderr.c_str(), "r");
  if (pipe == nullptr) {
    return {-1, ""};
  }

  std::string output;
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }

  const int status = pclose(pipe);

  if (status == -1) {
    return {-1, output};
  }

  if (WIFEXITED(status)) {
    return {WEXITSTATUS(status), output};
  }

  // The command was terminated by a signal.
  if (WIFSIGNALED(status)) {
    return {128 + WTERMSIG(status), output};
  }

  return {-1, output};
}

}  // namespace jiaolong
