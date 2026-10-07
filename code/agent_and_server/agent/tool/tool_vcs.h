#pragma once

#include <memory>
#include <string>
#include <utility>

#include "agent/tool/tool.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {

// Input and output for VcsCreateCommit.
class ToolVcsCreateCommitInput : public ToolUseInput {
 public:
  const std::string commit_message_;
  const std::string working_directory_;
  ToolVcsCreateCommitInput(const std::string& commit_message,
      const std::string& working_directory) :
      ToolUseInput(ToolType::kVcs), commit_message_(commit_message),
      working_directory_(working_directory) {};
};
class ToolVcsCreateCommitOutput : public ToolUseOutput {
 public:
  const std::string stdout_output_;
  ToolVcsCreateCommitOutput(const std::string& stdout_output) :
      ToolUseOutput(ToolType::kVcs), stdout_output_(stdout_output) {}
};

// Input and output for VcsInitBranch.
class ToolVcsInitBranchInput : public ToolUseInput {
 public:
  const std::string task_id_;
  const std::string working_directory_;
  ToolVcsInitBranchInput(const std::string& task_id,
      const std::string& working_directory) :
      ToolUseInput(ToolType::kVcs), task_id_(task_id),
      working_directory_(working_directory) {};
};
class ToolVcsInitBranchOutput : public ToolUseOutput {
 public:
  const std::string stdout_output_;
  ToolVcsInitBranchOutput(const std::string& stdout_output) :
      ToolUseOutput(ToolType::kVcs), stdout_output_(stdout_output) {}
};

// Input and output for VcsUploadCommit.
class ToolVcsUploadCommitInput : public ToolUseInput {
 public:
  const std::string task_id_;
  const std::string working_directory_;
  ToolVcsUploadCommitInput(const std::string& task_id = "",
      const std::string& working_directory = "") :
      ToolUseInput(ToolType::kVcs), task_id_(task_id),
      working_directory_(working_directory) {};
};
class ToolVcsUploadCommitOutput : public ToolUseOutput {
 public:
  const std::string stdout_output_;
  ToolVcsUploadCommitOutput(const std::string& stdout_output = "") :
      ToolUseOutput(ToolType::kVcs), stdout_output_(stdout_output) {}
};

// VCS tools backed by Jujutsu (jj). Since tasks are worked on sequentially,
// the commit being worked on is always the current top commit.
//
// CreateCommit is registered to the LLM request so the agent can create a
// commit for the task. InitBranch is driven by an explicit user action (the
// "Init Branch & start working" button on the task detail page) and
// UploadCommit is run automatically by the server once a task execution
// completes successfully. Neither is registered to the LLM request, since we
// currently do not want the LLM to call these two methods directly.

// Creates a commit with the given message by running
// `jj commit -m <message>` inside the working directory.
class ToolVcsCreateCommit : public Tool {

 public:

  explicit ToolVcsCreateCommit(const std::string& jj_executable_path) :
      Tool(ToolType::kVcs, PermissionType::kWrite),
      jj_executable_path_(jj_executable_path) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolVcsCreateCommitOutput>
  CreateCommit(const ToolVcsCreateCommitInput& input);

 private:

  const std::string jj_executable_path_;

};

// Initializes a new branch (bookmark) for the task by running
// `jj git fetch`, `jj new main@origin` and
// `jj bookmark create task_<task_id>` inside the working directory.
// This backs the "Init Branch" button and is not registered to the LLM.
class ToolVcsInitBranch : public Tool {

 public:

  ToolVcsInitBranch(const std::string& jj_executable_path,
      const std::string& socks_proxy) :
      Tool(ToolType::kVcs, PermissionType::kWrite),
      jj_executable_path_(jj_executable_path),
      socks_proxy_(socks_proxy) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolVcsInitBranchOutput>
  InitBranch(const ToolVcsInitBranchInput& input);

 private:

  const std::string jj_executable_path_;

  // SOCKS proxy address used by `jj git fetch` and `jj git push` when talking
  // to the remote.
  const std::string socks_proxy_;

};

// Uploads the current top commit to the remote GitHub repository and opens
// a pull request by running `jj git fetch`, `jj rebase -d main@origin`,
// `jj git push --bookmark task_<task_id>` and
// `<gh_executable_path> pr create --base main --head task_<task_id>` inside
// the working directory. The Jiaolong Server runs this automatically once a
// task execution completes successfully; it is not registered to the LLM.
class ToolVcsUploadCommit : public Tool {

 public:

  ToolVcsUploadCommit(const std::string& jj_executable_path,
      const std::string& github_token,
      const std::string& gh_executable_path,
      const std::string& socks_proxy) :
      Tool(ToolType::kVcs, PermissionType::kWrite),
      jj_executable_path_(jj_executable_path),
      github_token_(github_token),
      gh_executable_path_(gh_executable_path),
      socks_proxy_(socks_proxy) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolVcsUploadCommitOutput>
  UploadCommit(const ToolVcsUploadCommitInput& input);

 private:

  const std::string jj_executable_path_;

  const std::string github_token_;

  // Path to the GitHub CLI (gh) executable used to create pull requests. It
  // is configured in the settings file (`ghExecutablePath`).
  const std::string gh_executable_path_;

  // SOCKS proxy address used by `jj git fetch` and `jj git push` when talking
  // to the remote.
  const std::string socks_proxy_;

};

}  // namespace jiaolong
