#pragma once

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent/tool/tool_read.h"
#include "agent/tool/tool_edit.h"
#include "agent/tool/tool_delete.h"
#include "agent/tool/tool_move.h"
#include "agent/tool/tool_copy.h"
#include "agent/tool/tool_find.h"
#include "agent/tool/tool_list_directory.h"
#include "agent/tool/tool_vcs.h"
#include "agent/tool/tool_bash.h"
#include "agent/permission_checker/permission_checker.h"
#include "agent/permission_checker/mount_table.h"
#include "agent/agent_turn.h"
#include "agent/project.h"
#include "agent/task.h"

namespace jiaolong {

enum ToolPermission {
  kAllowAll,
  kAsk
};

// Why the last Agent::Work call did not stop the LLM naturally. It is only
// meaningful when Work returned false; WorkFailureReason::kNone marks a
// successful (naturally stopped) run. The values mirror the machine-readable
// `tasks.failure_reason` taxonomy described in
// doc/technology/task_failure_reason_system_design.md.
enum class WorkFailureReason {
  // The LLM stopped naturally (success).
  kNone,
  // The task token budget was exhausted before the LLM stopped naturally.
  kTokenNotEnough,
  // Any other failure: an LLM API error after all retries (non-2xx, timeout,
  // unparseable JSON, or a response missing `finish_reason`), an invalid task
  // working directory, or a crash/kill without a more specific reason.
  kExecutionFailure,
};

class Agent {

 public:

  // `forbidden_to_write_file_list` is the list of real file paths the agent is
  // not allowed to modify. It is a mandatory parameter: the CLI's main.cc
  // reads it from the settings file's `forbiddenToWriteFileList` field and
  // keeps only the entries that are absolute paths pointing to existing
  // regular files in the host file system; virtual paths (e.g.
  // `/workspace/foo.txt`) and any other non-file path are discarded there. It
  // falls back to an empty list when the field is absent or is not an array.
  Agent(const std::string& llm_api_base_url,
      const std::string& llm_model,
      const std::string& security_key,
      const int& llm_cooldown_duration,
      const int& llm_retry_times,
      const std::string& jj_executable_path,
      const std::string& github_token,
      const std::string& gh_executable_path,
      const std::vector<AllowedBashCommand>& allowed_bash_commands,
      const std::string& socks_proxy,
      const std::vector<std::string>& forbidden_to_write_file_list);

  // Overload that accepts `forbidden_to_write_file_list` right after the
  // allowed bash commands instead of at the end of the parameter list. It is
  // equivalent to the constructor above.
  Agent(const std::string& llm_api_base_url,
      const std::string& llm_model,
      const std::string& security_key,
      const int& llm_cooldown_duration,
      const int& llm_retry_times,
      const std::string& jj_executable_path,
      const std::string& github_token,
      const std::string& gh_executable_path,
      const std::vector<AllowedBashCommand>& allowed_bash_commands,
      const std::vector<std::string>& forbidden_to_write_file_list,
      const std::string& socks_proxy);

  void ClearMessages();

  void AppendSlashCommand(const std::string& slash_command);

  // Runs the agent on the given task. When `resume` is true, the agent first
  // tries to load the previous session history for the task from the
  // `~/.jiaolong/sessions` folder so it continues working on the task as if
  // it had never stopped; when no such history exists it falls back to
  // starting a fresh round. Returns true when the LLM stopped naturally (the
  // last LLM response had `finish_reason` "stop"), and false when it stopped
  // for any other reason (an LLM API error, the token limit was reached, or
  // the task working directory was invalid).
  bool Work(const Task& task, bool resume = false);

  // Same as `Work(task, resume)` but also receives the task's project. The
  // readonly directories of the project that the task selected become
  // read-only mounts (`/readonly/<alias>`) the agent can read in addition to
  // its `/workspace` working directory. When `project` has an empty catalog
  // (or the task selects none) this behaves exactly like `Work(task, resume)`.
  bool Work(const Task& task, const Project& project, bool resume = false);

  void SetToolPermission(ToolPermission tool_permission);

  // Returns the total number of tokens the agent has consumed across all LLM
  // API calls made by this agent instance so far. In non-interactive mode a
  // single agent instance works on exactly one task, so this equals the total
  // token usage of that task's run.
  int total_token_usage() const { return total_token_usage_; }

  // Why the last Work call did not stop the LLM naturally. Returns
  // WorkFailureReason::kNone when the last Work call succeeded; callers should
  // only read it after Work returned false.
  WorkFailureReason failure_reason() const { return failure_reason_; }

 private:

  std::string security_key_;
  std::string llm_model_;
  std::string llm_api_base_url_;

  int llm_cooldown_duration_;

  // Number of times to retry the LLM API call after a non-successful response.
  // A value of 0 (the default when `retryTimes` is not configured in the
  // settings file) means the agent fails fast without retrying.
  int llm_retry_times_ = 0;

  // Maximum duration to wait for a single LLM API request to complete. A
  // request that exceeds this timeout is aborted and treated as a failed
  // request (and is therefore retried while retries remain), so the agent
  // never waits forever for the LLM API. Defaults to 3 minutes.
  std::chrono::milliseconds llm_request_timeout_ = std::chrono::minutes(3);

  // The list of real file paths the agent is forbidden to modify. It is parsed
  // and preprocessed from the settings file's `forbiddenToWriteFileList` field
  // by the CLI's main.cc (which keeps only absolute paths pointing to existing
  // regular files) and passed in as a mandatory constructor argument. Tools
  // with the write permission type refuse to modify any file whose real path
  // matches an entry of this list; read-only tools are never checked against
  // it.
  std::vector<std::string> forbidden_to_write_file_list_;

  // The tools at the agent's disposal. Each tool supports exactly one usage
  // function.
  ToolReadWholeFile tool_read_whole_file_;
  ToolCountLines tool_count_lines_;
  ToolReadFileOfLineRange tool_read_file_of_line_range_;
  ToolSearchAndReplace tool_search_and_replace_;
  ToolCreateNewFile tool_create_new_file_;
  ToolCreateNewDirectory tool_create_new_directory_;
  ToolWriteContentToFile tool_write_content_to_file_;
  ToolDelete tool_delete_;
  ToolMove tool_move_;
  ToolCopy tool_copy_;
  ToolFindFiles tool_find_files_;
  ToolFindStrings tool_find_strings_;
  ToolListDirectory tool_list_directory_;
  ToolVcsCreateCommit tool_vcs_create_commit_;
  ToolBash tool_bash_;

  PermissionChecker permission_checker_;

  // Builds the task's mount table (primary `/workspace` mount plus the
  // readonly directories the task selected) and configures the permission
  // checker with it and the forbidden-to-write file list.
  void ConfigureMountTable(const Task& task, const Project& project);

  // Prepends a short, deterministic file-system-layout block to the system
  // prompt when the task has at least one read-only mount, so the LLM knows
  // which `/readonly/<alias>` roots exist. Does nothing when there is no
  // read-only mount, keeping the pre-feature prompt unchanged.
  void UpdateSystemPromptWithMounts();

  ToolPermission tool_permission_ = ToolPermission::kAllowAll;

  nlohmann::json request_json_;
  std::vector<nlohmann::json> messages_;
  int total_token_usage_ = 0;
  bool llm_stopped_ = false;
  // Why the last Work call terminated. Set at every terminal point of Work;
  // WorkFailureReason::kNone marks a natural stop (success).
  WorkFailureReason failure_reason_ = WorkFailureReason::kNone;

  // Whether LLM API response is loading.
  std::atomic<bool> is_llm_loading_{false};

  // Thread to show loading UI.
  std::thread show_loading_thread_;

  std::string llm_loading_message_ = "Thinking...";

  std::vector<int> llm_response_durations_in_second_;

  std::string session_id_;
  std::string session_creation_timestamp_;

  // The id of the task currently associated with this session. It is empty
  // until the agent starts working on a task (Agent::Work), after which it is
  // persisted into the session history JSON file.
  std::string task_id_;

  std::vector<AgentTurn> agent_turns_;

  nlohmann::json ConstructNewUserMessage(const Task& task);

  void AppendMessage(const nlohmann::json& message);

  void PersistSessionHistory();

  // Tries to load the session history of the given task into the agent's
  // current state (messages, request messages and agent turns). It scans the
  // `~/.jiaolong/sessions` folder for session history JSON files whose
  // `taskId` matches `task_id` and, when several match, picks the most
  // recently modified one. Returns true and restores the session (including
  // its session id) on success; returns false when HOME is not set, the
  // sessions folder does not exist, no matching session history file is found,
  // or the file cannot be read/parsed.
  bool LoadSessionHistory(const std::string& task_id);

  // Sends the request to the LLM API and returns the parsed chat completion
  // response. The request times out after `llm_request_timeout_` (3 minutes);
  // a timed-out request is treated as a failed request and is retried while
  // retries remain. Returns std::nullopt when the API returns a non-success
  // status code or the request times out (after all retries are exhausted), or
  // when the response body is not valid JSON / is missing the mandatory
  // `finish_reason` field; in those cases an error is logged before returning.
  std::optional<nlohmann::json> SendMessages();

  // Parses an LLM chat completion response. `finish_reason` is the only
  // mandatory field; when it is missing, null, or not a string, logs an error
  // to stderr and returns false. All other fields are optional and fall back
  // to an empty string (or 0 for total token usage) when they are missing,
  // null, or of an unexpected type. Returns true on success.
  bool ParseLlmResponse(const nlohmann::json& response_json,
      std::string& finish_reason,
      std::string& reasoning_content,
      std::string& content,
      int& total_tokens);

  void ExecuteToolCalls(const nlohmann::json& tool_calls,
      const std::string& task_working_directory);

  // Returns true when `real_path` matches an entry of the forbidden-to-write
  // file list. The forbidden-to-write list only holds real file paths in the
  // host file system (main.cc discards virtual and other non-file entries
  // before the agent is constructed), so this is a plain real-path match;
  // callers map the tool's virtual path to its real path first.
  bool IsForbiddenToWrite(const std::string& real_path) const;

  // Returns true when `tool` has the write permission type and `real_path`
  // matches an entry of the forbidden-to-write file list (see the overload
  // above). Read-only tools are never checked, so this is the shared permission
  // check every tool execution function uses.
  bool IsForbiddenToWrite(const Tool& tool,
      const std::string& real_path) const;

  void ShowLlmLoading();

  void DismissLlmLoading();

  float CalculateAverageLlmResponseDurationInSecond();

  void ExecuteToolSearchAndReplace(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

  void ExecuteToolCreateNewFile(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

  void ExecuteToolCreateNewDirectory(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

  void ExecuteToolWriteContentToFile(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

  void ExecuteToolDelete(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

  void ExecuteToolFindFiles(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

  void ExecuteToolFindStrings(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

  void ExecuteToolListDirectory(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

  void ExecuteToolMove(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

  void ExecuteToolCopy(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

  void ExecuteToolReadWholeFile(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

  void ExecuteToolReadFile(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

  void ExecuteToolVcsCreateCommit(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

  void ExecuteToolBash(nlohmann::json& tool_message,
      const nlohmann::json& args_json,
      const std::string& task_working_directory);

};

}  // namespace jiaolong
