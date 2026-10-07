#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "agent/tool/tool_bash.h"
#include "agent/tool/tool_use_status.h"
#include "util/command_runner.h"

namespace jiaolong {

namespace {

// Wraps a value in double quotes and escapes embedded double quotes and
// backslashes so it can be embedded safely in a single shell command line.
std::string ShellQuote(const std::string& value) {
  std::string quoted = "\"";
  for (const char c : value) {
    if (c == '\\' || c == '"') {
      quoted += '\\';
    }
    quoted += c;
  }
  quoted += '"';
  return quoted;
}

}  // namespace

std::pair<std::shared_ptr<ToolUseStatus>, ToolBashOutput>
ToolBash::Execute(const ToolBashInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  if (input.bash_command_.empty()) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(
          duration_in_ms,
          "Invalid input: bash command must not be empty"),
      ToolBashOutput("")
    };
  }

  if (input.working_directory_.empty()) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(
          duration_in_ms,
          "Invalid input: working directory must not be empty"),
      ToolBashOutput("")
    };
  }

  // Only execute the exact `bashCommand` / `workingDirectory` pairs configured
  // in the settings file (`allowedBashCommands` field).
  bool is_allowed = false;
  for (const AllowedBashCommand& allowed_command : allowed_bash_commands_) {
    if (allowed_command.bash_command == input.bash_command_ &&
        allowed_command.working_directory == input.working_directory_) {
      is_allowed = true;
      break;
    }
  }

  if (!is_allowed) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(
          duration_in_ms,
          "Invalid input: bash command is not in the allowed list, command: \"" +
              input.bash_command_ + "\", working directory: \"" +
              input.working_directory_ + "\""),
      ToolBashOutput("")
    };
  }

  // Run the command with bash inside the working directory and capture the
  // combined stdout and stderr (the utility appends `2>&1`).
  const std::string command = "cd " + ShellQuote(input.working_directory_) +
      " && bash -c " + ShellQuote(input.bash_command_);
  const auto exit_code_and_output = RunCommandAndCaptureOutput(command);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();

  if (exit_code_and_output.first != 0) {
    return {
      std::make_shared<ToolUseExecutionFailure>(
          duration_in_ms,
          "bash command failed with exit code " +
              std::to_string(exit_code_and_output.first) + ": " +
              exit_code_and_output.second),
      ToolBashOutput(exit_code_and_output.second)
    };
  }

  return {
    std::make_shared<ToolUseSuccess>(duration_in_ms),
    ToolBashOutput(exit_code_and_output.second)
  };
}

}  // namespace jiaolong