#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <iostream>

#include "agent/tool/tool_vcs.h"
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

std::pair<std::shared_ptr<ToolUseStatus>, ToolVcsCreateCommitOutput>
ToolVcsCreateCommit::CreateCommit(const ToolVcsCreateCommitInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  if (input.commit_message_.empty() || input.working_directory_.empty()) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(
          duration_in_ms,
          "Invalid input: commit message and working directory must not be "
          "empty"),
      ToolVcsCreateCommitOutput("")
    };
  }

  // Run `jj commit -m <message>` inside the task working directory.
  const std::string command = "cd " + ShellQuote(input.working_directory_) +
      " && " + jj_executable_path_ + " commit -m " +
      ShellQuote(input.commit_message_);
  const auto exit_code_and_output = RunCommandAndCaptureOutput(command);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();

  if (exit_code_and_output.first != 0) {
    return {
      std::make_shared<ToolUseExecutionFailure>(
          duration_in_ms,
          "jj commit failed with exit code " +
              std::to_string(exit_code_and_output.first) + ": " +
              exit_code_and_output.second),
      ToolVcsCreateCommitOutput(exit_code_and_output.second)
    };
  }

  return {
    std::make_shared<ToolUseSuccess>(duration_in_ms),
    ToolVcsCreateCommitOutput(exit_code_and_output.second)
  };
}

std::pair<std::shared_ptr<ToolUseStatus>, ToolVcsInitBranchOutput>
ToolVcsInitBranch::InitBranch(const ToolVcsInitBranchInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  if (input.task_id_.empty() || input.working_directory_.empty()) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(
          duration_in_ms,
          "Invalid input: task id and working directory must not be empty"),
      ToolVcsInitBranchOutput("")
    };
  }

  // The working directory must exist and be a directory; otherwise the `cd`
  // in the command below fails without any captured output and the user only
  // sees an unhelpful "failed with exit code 1" error. Report this specific
  // case up front so the client knows the workspace is missing. The
  // non-throwing overload is used so filesystem errors (e.g. permission
  // denied) are reported through the error code instead of throwing an
  // exception.
  std::error_code working_directory_error_code;
  const bool working_directory_is_directory = std::filesystem::is_directory(
      input.working_directory_, working_directory_error_code);
  if (working_directory_error_code || !working_directory_is_directory) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(
          duration_in_ms,
          "Invalid input: workspace (working directory) does not exist or is "
          "not a directory: " + input.working_directory_),
      ToolVcsInitBranchOutput("")
    };
  }

  // Run `jj git fetch`, `jj new main@origin` and
  // `jj bookmark create task_<task_id>` inside the task working directory.
  // The whole chain is wrapped in a subshell so that the `2>&1` appended by
  // the command runner captures the stderr of every command (including a
  // failing `cd`) and can be returned to the client instead of just the last
  // command's stderr.
  const std::string jj_bookmark = "task_" + input.task_id_;
  const std::string command = "(cd " + ShellQuote(input.working_directory_) +
      " && SOCKS_PROXY=" + socks_proxy_ + " " + jj_executable_path_ + " git fetch" +
      " && " + jj_executable_path_ + " new main@origin" +
      " && " + jj_executable_path_ + " bookmark delete " + jj_bookmark +
      " && SOCKS_PROXY=" + socks_proxy_ + " " + jj_executable_path_ + " git push --deleted" +
      " && " + jj_executable_path_ + " bookmark create " + jj_bookmark +
      ")";
  const auto exit_code_and_output = RunCommandAndCaptureOutput(command);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();

  if (exit_code_and_output.first != 0) {
    return {
      std::make_shared<ToolUseExecutionFailure>(
          duration_in_ms,
          "jj init branch failed with exit code " +
              std::to_string(exit_code_and_output.first) + ": " +
              exit_code_and_output.second),
      ToolVcsInitBranchOutput(exit_code_and_output.second)
    };
  }

  return {
    std::make_shared<ToolUseSuccess>(duration_in_ms),
    ToolVcsInitBranchOutput(exit_code_and_output.second)
  };
}

std::pair<std::shared_ptr<ToolUseStatus>, ToolVcsUploadCommitOutput>
ToolVcsUploadCommit::UploadCommit(const ToolVcsUploadCommitInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  if (input.task_id_.empty() || input.working_directory_.empty()) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(
          duration_in_ms,
          "Invalid input: task id and working directory must not be empty"),
      ToolVcsUploadCommitOutput("")
    };
  }

  // Run `jj git fetch`, `jj rebase -d main@origin`,
  // `jj git push --bookmark task_<task_id>` and
  // `<gh_executable_path> pr create --fill-first --base main --head task_<task_id>` inside
  // the task working directory. The whole chain is wrapped in a subshell so that
  // the `2>&1` appended by the command runner captures the stderr of every
  // command (in particular a failing `jj git push`, whose diagnostics are
  // printed to stderr) and can be returned to the client instead of just the
  // last command's stderr.
  const std::string command = "(cd " + ShellQuote(input.working_directory_) +  
      " && GIT_SSH_COMMAND=\"ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null\" SOCKS_PROXY=" + socks_proxy_ + " " + jj_executable_path_ + " git fetch" +
      " && " + jj_executable_path_ + " rebase -d main@origin" +
      " && GIT_SSH_COMMAND=\"ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null\" SOCKS_PROXY=" + socks_proxy_ + " " + jj_executable_path_ + " git push --bookmark task_" + input.task_id_ +
      " && GITHUB_TOKEN=" + github_token_ + " " + gh_executable_path_ +
      " pr create --fill-first --base main --head task_" + input.task_id_ + ")";
  const auto exit_code_and_output = RunCommandAndCaptureOutput(command);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();

  if (exit_code_and_output.first != 0) {
    std::cerr << "UploadCommit: command execution failure, output: " << exit_code_and_output.second << std::endl;
    return {
      std::make_shared<ToolUseExecutionFailure>(
          duration_in_ms,
          "jj upload commit failed with exit code " +
              std::to_string(exit_code_and_output.first) + ": " +
              exit_code_and_output.second),
      ToolVcsUploadCommitOutput(exit_code_and_output.second)
    };
  }

  return {
    std::make_shared<ToolUseSuccess>(duration_in_ms),
    ToolVcsUploadCommitOutput(exit_code_and_output.second)
  };
}

}  // namespace jiaolong
