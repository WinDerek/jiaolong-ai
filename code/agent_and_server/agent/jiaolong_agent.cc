#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <format>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <utility>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include "agent/jiaolong_agent.h"
#include "agent/tool/tool_use_status.h"
#include "agent/tool/tool_read.h"
#include "agent/tool/tool_edit.h"
#include "agent/tool/tool_delete.h"
#include "agent/tool/tool_move.h"
#include "agent/tool/tool_copy.h"
#include "agent/tool/tool_find.h"
#include "agent/tool/tool_list_directory.h"
#include "agent/tool/tool_vcs.h"
#include "agent/tool/tool_bash.h"
#include "util/json_utils.h"
#include "util/uuid_generator.h"

namespace jiaolong {

namespace {

std::string EscapeNewlines(const std::string& input) {
  std::string result;
  for (char c : input) {
    if (c == '\n') {
      result += "\\n"; // Inserts literal '\' and 'n'
    } else if (c == '\r') {
      result += "\\r"; // Optional: handles Windows carriage returns
    } else {
      result += c;
    }
  }
  return result;
}

std::string GetCurrentTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time;
  localtime_r(&now_time, &local_time);
  std::ostringstream oss;
  oss << std::put_time(&local_time, "%Y%m%d_%H%M%S");
  return oss.str();
}



// Returns an empty string when `json` is an object that carries `field` as a
// string; otherwise returns a human-readable reason explaining why the field
// could not be read. Unlike `RequireStringJsonField` in `util/json_utils.h`,
// this never exits the process, so callers can turn a malformed field into an
// error response.
std::string StringFieldErrorMessage(const nlohmann::json& json,
    const std::string& field) {
  if (!json.is_object() || !json.contains(field)) {
    return "JSON is missing required field \"" + field + "\".";
  }
  if (!json[field].is_string()) {
    return "JSON field \"" + field + "\" must be a string.";
  }
  return "";
}

// Same as `StringFieldErrorMessage` but for integer fields.
std::string IntFieldErrorMessage(const nlohmann::json& json,
    const std::string& field) {
  if (!json.is_object() || !json.contains(field)) {
    return "JSON is missing required field \"" + field + "\".";
  }
  if (!json[field].is_number_integer()) {
    return "JSON field \"" + field + "\" must be an integer.";
  }
  return "";
}

// Reads a string argument from a tool call's arguments without ever exiting
// the process. On failure it fills `tool_message["content"]` with an error
// message so that a malformed LLM tool call is reported back to the LLM
// instead of crashing the agent, and returns false.
bool TryReadStringArg(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& field,
    std::string& out) {
  const std::string error_message = StringFieldErrorMessage(args_json, field);
  if (!error_message.empty()) {
    tool_message["content"] =
        "invalid input, error_message: " + error_message;
    return false;
  }
  out = args_json[field].get<std::string>();
  return true;
}

// Same as `TryReadStringArg` but for integer arguments.
bool TryReadIntArg(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& field,
    int& out) {
  const std::string error_message = IntFieldErrorMessage(args_json, field);
  if (!error_message.empty()) {
    tool_message["content"] =
        "invalid input, error_message: " + error_message;
    return false;
  }
  out = args_json[field].get<int>();
  return true;
}

}  // namespace

bool Agent::IsForbiddenToWrite(const std::string& real_path) const {
  for (const std::string& forbidden_path : forbidden_to_write_file_list_) {
    if (forbidden_path == real_path) {
      return true;
    }
  }
  return false;
}

bool Agent::IsForbiddenToWrite(const Tool& tool,
    const std::string& real_path) const {
  // Only tools with the write permission type are checked against the
  // forbidden-to-write file list; read-only tools are never blocked.
  if (tool.GetPermissionType() != PermissionType::kWrite) {
    return false;
  }
  return IsForbiddenToWrite(real_path);
}

void Agent::ConfigureMountTable(const Task& task, const Project& project) {
  PermissionContext context;
  context.mount_table = MountTable::FromTask(task, project);
  context.forbidden_to_write = forbidden_to_write_file_list_;
  permission_checker_ = PermissionChecker(std::move(context));
}

void Agent::UpdateSystemPromptWithMounts() {
  // Nothing to disclose when the task has no read-only mount: keep the system
  // prompt exactly as before so backward compatibility is preserved.
  if (permission_checker_.mount_table().readonly_mount_count() == 0) {
    return;
  }

  std::string layout =
      "\n\nFile system layout for this task:\n"
      "  /workspace             (read + write)  primary task working directory\n";
  for (const Mount& mount : permission_checker_.mount_table().mounts()) {
    if (mount.access != MountAccess::kReadOnly) {
      continue;
    }
    layout += "  " + mount.virtual_root + "         (read only)";
    if (!mount.description.empty()) {
      layout += "     " + mount.description;
    }
    layout += "\n";
  }
  layout +=
      "\nRules:\n"
      "  - Paths must be absolute and under one of the roots above.\n"
      "  - Read-only roots cannot be modified. To change a readonly file, "
      "copy it into /workspace first and edit the copy.";

  // The system message is the first message of the conversation. Append the
  // layout block to it and keep messages_ and the request body in sync.
  if (!messages_.empty() && messages_[0].is_object() &&
      messages_[0].contains("content") && messages_[0]["content"].is_string()) {
    messages_[0]["content"] =
        messages_[0]["content"].get<std::string>() + layout;
    request_json_["messages"][0]["content"] =
        messages_[0]["content"];
    if (!agent_turns_.empty()) {
      agent_turns_[0] = AgentTurn(messages_[0]);
    }
  }
}

Agent::Agent(const std::string& llm_api_base_url,
    const std::string& llm_model,
    const std::string& security_key,
    const int& llm_cooldown_duration,
    const int& llm_retry_times,
    const std::string& jj_executable_path,
    const std::string& github_token,
    const std::string& gh_executable_path,
    const std::vector<AllowedBashCommand>& allowed_bash_commands,
    const std::vector<std::string>& forbidden_to_write_file_list,
    const std::string& socks_proxy)
    : Agent(llm_api_base_url, llm_model, security_key, llm_cooldown_duration,
        llm_retry_times, jj_executable_path, github_token, gh_executable_path,
        allowed_bash_commands, socks_proxy, forbidden_to_write_file_list) {}

Agent::Agent(const std::string& llm_api_base_url,
    const std::string& llm_model,
    const std::string& security_key,
    const int& llm_cooldown_duration,
    const int& llm_retry_times,
    const std::string& jj_executable_path,
    const std::string& github_token,
    const std::string& gh_executable_path,
    const std::vector<AllowedBashCommand>& allowed_bash_commands,
    const std::string& socks_proxy,
    const std::vector<std::string>& forbidden_to_write_file_list):
    security_key_(security_key),
    llm_api_base_url_(llm_api_base_url), llm_model_(llm_model),
    llm_cooldown_duration_(llm_cooldown_duration),
    llm_retry_times_(llm_retry_times),
    forbidden_to_write_file_list_(forbidden_to_write_file_list),
    tool_vcs_create_commit_(jj_executable_path),
    tool_bash_(allowed_bash_commands) {
  // Generate session id and creation timestamp.
  session_id_ = GenerateUuid();
  session_creation_timestamp_ = GetCurrentTimestamp();
  std::cout << "session id: " << session_id_ << ", creation timestamp: " <<
      session_creation_timestamp_ << std::endl;

  // Initialize messages on start up.
  ClearMessages();

  // Record the initial (system) message as an agent turn.
  for (const nlohmann::json& message : messages_) {
    agent_turns_.push_back(AgentTurn(message));
  }

  // Persist the initial session history eagerly.
  PersistSessionHistory();
}

bool Agent::Work(const Task& task, bool resume) {
  return Work(task, Project(), resume);
}

bool Agent::Work(const Task& task, const Project& project, bool resume) {
  std::cout << ">> task: title: \"" << task.title_ <<
      "\", description: \"" << task.description_ <<
      "\", working directory: " << task.working_directory_ << std::endl;

  // Start each run with no recorded failure; it is updated at every terminal
  // point below so callers can classify why Work returned false.
  failure_reason_ = WorkFailureReason::kNone;

  // The task working directory is legal if and only if it is a directory. A
  // single trailing slash `/` is allowed and is simply removed before
  // continuing with the rest of the logic; multiple trailing slashes are
  // illegal. Fail fast (without sending any LLM API call) when it is invalid.
  std::string working_directory = task.working_directory_;
  if (working_directory.ends_with('/')) {
    // Count the trailing slashes to tell apart a single trailing slash (which
    // is normalized away) from multiple trailing slashes (which are illegal).
    size_t trailing_slash_count = 0;
    while (trailing_slash_count < working_directory.size() &&
        working_directory[working_directory.size() - 1 - trailing_slash_count] == '/') {
      ++trailing_slash_count;
    }
    if (trailing_slash_count > 1) {
      std::cout << "task working directory must not end with multiple slashes "
          "(`/`): " << task.working_directory_ << std::endl;
      failure_reason_ = WorkFailureReason::kExecutionFailure;
      return false;
    }
    working_directory.pop_back();
  }
  if (!std::filesystem::is_directory(working_directory)) {
    std::cerr << "the specified task working directory does not exist or is "
        "not a directory, abort task" << std::endl;
    failure_reason_ = WorkFailureReason::kExecutionFailure;
    return false;
  }

  // Associate this session with the task being worked on so the persisted
  // session history JSON carries the task id.
  task_id_ = task.id_;

  // Build the task's mount table (primary `/workspace` mount plus the readonly
  // directories the task selected) and configure the permission checker with
  // it, so every tool call is resolved against the same namespace.
  ConfigureMountTable(task, project);

  // When resuming, try to load the previous session history for this task so
  // the agent continues working where it left off instead of starting a fresh
  // round. If no history exists, fall back to starting fresh.
  bool history_loaded = false;
  if (resume) {
    history_loaded = LoadSessionHistory(task.id_);
    if (!history_loaded) {
      std::cout << "No previous session history found for task " << task.id_ <<
          ", starting a fresh round." << std::endl;
    }
  }

  auto time_begin = std::chrono::steady_clock::now();

  // Append the initial message about the task from user, unless the previous
  // session history was loaded (in which case the conversation already
  // contains the task's initial user message).
  if (!history_loaded) {
    UpdateSystemPromptWithMounts();
    nlohmann::json initial_user_message = ConstructNewUserMessage(task);
    AppendMessage(initial_user_message);
  }

  int task_token_usage = 0;
  bool first_llm_request = true;
  while (true) {
    // // Show current messages about to send
    // std::cout << std::endl << "Messages:" << std::endl;
    // for (const nlohmann::json& message : messages_) {
    //   std::string role_string = message["role"].get<std::string>();
    //   if (role_string == "tool") {
    //     role_string += "[" + message["tool_call_id"].get<std::string>() + "]";
    //   }
    //   std::cout << "<" << role_string << ">: " <<
    //       message["content"] << std::endl << std::endl;
    // }

    // std::cout << "Press Enter to continue..." << std::endl;
    // // Clear the input buffer just in case there are leftover newline characters
    // std::cin.clear();
    // std::cin.sync(); 
    // // Wait for the Enter key
    // std::cin.get();

    // Send messages to LLM
    // Except for the first request, we will apply an LLM cooldown.
    if (!first_llm_request) {
      std::this_thread::sleep_for(std::chrono::milliseconds(
          llm_cooldown_duration_));
    } else {
      first_llm_request = false;
    }

    const std::optional<nlohmann::json> response = SendMessages();
    if (!response.has_value()) {
      std::cout << "[error] Failed to send messages to LLM." << std::endl;
      // The LLM API request failed after all retries; without a response the
      // task cannot have stopped naturally.
      failure_reason_ = WorkFailureReason::kExecutionFailure;
      break;
    }
    const nlohmann::json& response_json = *response;

    // Append new message from LLM
    const nlohmann::json& choices = RequireJsonField(response_json, "choices");
    AppendMessage(RequireJsonField(choices[0], "message"));

    // Update token usage data
    int current_token_usage =
        RequireIntJsonField(RequireJsonField(response_json, "usage"),
            "total_tokens");
    task_token_usage += current_token_usage;
    total_token_usage_ += current_token_usage;

    // Execute tool calls if exist
    if (response_json.contains("choices") &&
        response_json["choices"].is_array() &&
        !response_json["choices"].empty() &&
        response_json["choices"][0].is_object() &&
        response_json["choices"][0].contains("message") &&
        response_json["choices"][0]["message"].is_object() &&
        response_json["choices"][0]["message"].contains("tool_calls") &&
        response_json["choices"][0]["message"]["tool_calls"].is_array() &&
        !response_json["choices"][0]["message"]["tool_calls"].empty()) {
      const nlohmann::json tool_calls =
          response_json["choices"][0]["message"]["tool_calls"];
      ExecuteToolCalls(tool_calls, working_directory);
    }

    // Stop loop if LLM stopped naturally or total token usage exceeds limit.
    if (llm_stopped_ || task_token_usage >= task.token_limit_) {
      std::cout << "llm stopped: " << (llm_stopped_ ? "true" : "false") <<
          ", task token usage: " << task_token_usage <<
          ", total token usage: " << total_token_usage_ <<
          ", avg llm response time: " << std::setprecision(2) <<
          CalculateAverageLlmResponseDurationInSecond() <<
          " seconds" << std::endl;
      if (!llm_stopped_) {
        // The loop only exits here without a natural stop when the task token
        // budget was exhausted.
        failure_reason_ = WorkFailureReason::kTokenNotEnough;
      }
      break;
    }
  }

  auto time_end = std::chrono::steady_clock::now();
  auto duration_in_second = std::chrono::duration_cast<std::chrono::seconds>(
      time_end - time_begin);
  std::cout << "Total time usage: " << duration_in_second.count() <<
      " seconds" << std::endl;

  // The task succeeded only when the last LLM response we received indicated
  // that the LLM stopped naturally (finish_reason "stop"). When the loop
  // exited because of an LLM API error or because the token limit was
  // reached, the LLM did not stop and the task must be reported as failed.
  return llm_stopped_;
}

std::optional<nlohmann::json> Agent::SendMessages() {
  // The request is retried up to `llm_retry_times_` times when the LLM API
  // does not return a successful response, including when the request times
  // out after `llm_request_timeout_`. When `llm_retry_times_` is 0 (the
  // default when `retryTimes` is not configured in the settings file), the
  // agent fails fast on the first failed request, preserving the original
  // behavior.
  int retries_used = 0;
  while (true) {
    // Show loading for this attempt.
    ShowLlmLoading();

    auto time_begin = std::chrono::steady_clock::now();

    cpr::Response response = cpr::Post(cpr::Url{llm_api_base_url_},
        cpr::Body{request_json_.dump(-1, ' ', false,
            nlohmann::json::error_handler_t::replace)},
        cpr::Header{
          { "Content-Type", "application/json" },
          { "Accept", "application/json" },
          { "Authorization", "Bearer " + security_key_ }
        },
        // Abort the request when the LLM API does not respond within
        // `llm_request_timeout_` (3 minutes by default), so the agent never
        // waits forever.
        cpr::Timeout{llm_request_timeout_});

    auto time_end = std::chrono::steady_clock::now();
    auto duration_in_second = std::chrono::duration_cast<std::chrono::seconds>(
        time_end - time_begin);
    llm_response_durations_in_second_.push_back(duration_in_second.count());

    // Dismiss loading
    DismissLlmLoading();

    std::cout << "[model] status code: " << response.status_code << std::endl;

    // A request that exceeds `llm_request_timeout_` is aborted by cpr, which
    // reports it as a transport error (typically with status code 0 because no
    // complete response was received). Treat any such request failure as a
    // failed request even when it does not carry a non-success status code, so
    // a timed-out request goes through the same retry logic as any other
    // non-successful response.
    const bool request_failed =
        response.error.code != cpr::ErrorCode::OK ||
        response.status_code < 200 || response.status_code >= 300;
    if (request_failed) {
      // Log the likely timeout explicitly to make it easy to diagnose.
      if (response.status_code == 0) {
        std::cout << "[model] LLM API request failed without an HTTP "
            "response (possibly timed out after " <<
            llm_request_timeout_.count() / 1000 << " seconds)." << std::endl;
      }
      if (retries_used < llm_retry_times_) {
        ++retries_used;
        std::cout << "[model] LLM API returned status code " <<
            response.status_code << ", retrying (" << retries_used << "/" <<
            llm_retry_times_ << ")..." << std::endl;
        continue;
      }
      std::cerr << "[error] LLM API returned status code " <<
          response.status_code << ", response text: " << response.text <<
          std::endl;
      return std::nullopt;
    }

    // Parse the response json without relying on exceptions.
    nlohmann::json response_json =
        nlohmann::json::parse(response.text, nullptr, false);
    if (response_json.is_discarded()) {
      std::cerr << "[error] Failed to parse LLM API response as JSON."
          << std::endl;
      return std::nullopt;
    }

    // Extract the response fields robustly. `finish_reason` is the only
    // mandatory field; ParseLlmResponse logs an error to stderr and returns
    // false when it is missing, null, or of a different type. The remaining
    // fields are optional and fall back to an empty string (or 0 for token
    // usage).
    std::string finish_reason;
    std::string llm_response_reasoning_content;
    std::string llm_response_content;
    int llm_token_usage = 0;
    if (!ParseLlmResponse(response_json, finish_reason,
        llm_response_reasoning_content, llm_response_content, llm_token_usage)) {
      return std::nullopt;
    }
    std::cout << "[model] reasoning: " << llm_response_reasoning_content << std::endl;
    std::cout << "[model] content: " << llm_response_content << std::endl;
    std::cout << "[model] finish reason: " << finish_reason << std::endl;
    std::cout << std::endl << "[model] token usage: " << llm_token_usage << std::endl;

    llm_stopped_ = (finish_reason == "stop");

    return response_json;
  }
}

bool Agent::ParseLlmResponse(const nlohmann::json& response_json,
    std::string& finish_reason,
    std::string& reasoning_content,
    std::string& content,
    int& total_tokens) {
  // `finish_reason` is the only mandatory field. Fail fast with an error log
  // to stderr when it is missing, null, or of an unexpected type.
  const bool has_choice = response_json.contains("choices") &&
      response_json["choices"].is_array() &&
      !response_json["choices"].empty();
  const bool has_finish_reason = has_choice &&
      response_json["choices"][0].contains("finish_reason") &&
      response_json["choices"][0]["finish_reason"].is_string();
  if (!has_finish_reason) {
    std::cerr << "[error] LLM response is missing the mandatory field "
        "\"finish_reason\"." << std::endl;
    return false;
  }
  finish_reason = response_json["choices"][0]["finish_reason"].get<std::string>();

  // All other fields are optional; fall back to an empty string (or 0 for
  // token usage) when they are missing, null, or of a different type.
  const nlohmann::json& choice = response_json["choices"][0];
  reasoning_content = "";
  content = "";
  if (choice.contains("message") && choice["message"].is_object()) {
    const nlohmann::json& message = choice["message"];
    if (message.contains("reasoning_content") &&
        message["reasoning_content"].is_string()) {
      reasoning_content = message["reasoning_content"].get<std::string>();
    }
    if (message.contains("content") && message["content"].is_string()) {
      content = message["content"].get<std::string>();
    }
  }
  total_tokens = 0;
  if (response_json.contains("usage") && response_json["usage"].is_object() &&
      response_json["usage"].contains("total_tokens") &&
      response_json["usage"]["total_tokens"].is_number_integer()) {
    total_tokens = response_json["usage"]["total_tokens"].get<int>();
  }
  return true;
}

void Agent::ClearMessages() {
  // Initialize the messages
  std::string request_json_str = std::format(R"({{
    "model": "{}",
    "messages": [
      {{"role": "system", "content": "You are an intelligent assistant."}}
    ],
    "thinking": {{"type": "enabled"}},
    "reasoning_effort": "high",
    "stream": false,
    "tools": [
      {{
        "type": "function",
        "function": {{
          "name": "SearchAndReplace",
          "description": "Search `old_str` and replace it with `new_str` for a specified file.",
          "parameters": {{
            "type": "object",
            "properties": {{
              "filePath": {{
                "type": "string",
                "description": "Path of the file to edit"
              }},
              "oldStr": {{
                "type": "string",
                "description": "Old string"
              }},
              "newStr": {{
                "type": "string",
                "description": "New string"
              }}
            }}
          }}
        }}
      }},
      {{
        "type": "function",
        "function": {{
          "name": "CreateNewFile",
          "description": "Create a new empty file at the specified path. Parent directories will be created if they do not exist.",
          "parameters": {{
            "type": "object",
            "properties": {{
              "filePath": {{
                "type": "string",
                "description": "Path of the file to create"
              }},
              "initialContent": {{
                "type": "string",
                "description": "Initial content of the file"
              }}
            }}
          }}
        }}
      }},
      {{
        "type": "function",
        "function": {{
          "name": "WriteContentToFile",
          "description": "Write content to a file, overwriting its current content. This works even if the file is empty or contains only a newline character.",
          "parameters": {{
            "type": "object",
            "properties": {{
              "filePath": {{
                "type": "string",
                "description": "Path of the file to write"
              }},
              "content": {{
                "type": "string",
                "description": "Content to write to the file"
              }}
            }}
          }}
        }}
      }},
      {{
        "type": "function",
        "function": {{
          "name": "CreateNewDirectory",
          "description": "Create a new empty directory at the specified path. Parent directories will be created if they do not exist.",
          "parameters": {{
            "type": "object",
            "properties": {{
              "directoryPath": {{
                "type": "string",
                "description": "Path of the directory to create"
              }}
            }}
          }}
        }}
      }},
      {{
        "type": "function",
        "function": {{
          "name": "Delete",
          "description": "Delete a single file or directory. If the path refers to a directory, the directory and all of its contents are deleted recursively.",
          "parameters": {{
            "type": "object",
            "properties": {{
              "path": {{
                "type": "string",
                "description": "Path of the file or directory to delete"
              }}
            }}
          }}
        }}
      }},
      {{
        "type": "function",
        "function": {{
          "name": "FindFiles",
          "description": "Recursively find files whose file name contains the query under the root path. The query must not be empty. Skips common generated/vendored folders such as node_modules, build, dist and .git.",
          "parameters": {{
            "type": "object",
            "properties": {{
              "rootPath": {{
                "type": "string",
                "description": "Root path of the directory to search"
              }},
              "query": {{
                "type": "string",
                "description": "Query string to search for in filenames"
              }}
            }}
          }}
        }}
      }},
      {{
        "type": "function",
        "function": {{
          "name": "FindStrings",
          "description": "Recursively find files containing the query string under the root path and report the line numbers where it appears. The query must not be empty. Skips common generated/vendored folders such as node_modules, build, dist and .git.",
          "parameters": {{
            "type": "object",
            "properties": {{
              "rootPath": {{
                "type": "string",
                "description": "Root path of the directory to search"
              }},
              "query": {{
                "type": "string",
                "description": "Query string to search for in file content"
              }}
            }}
          }}
        }}
      }},
      {{
        "type": "function",
        "function": {{
          "name": "ListDirectory",
          "description": "List the files and directories in the specified directory. Only the immediate children are listed; use multiple ListDirectory calls to explore nested directories.",
          "parameters": {{
            "type": "object",
            "properties": {{
              "directoryPath": {{
                "type": "string",
                "description": "Path of the directory to list"
              }}
            }}
          }}
        }}
      }},
      {{
        "type": "function",
        "function": {{
          "name": "ReadWholeFile",
          "description": "Read file content as a whole string.",
          "parameters": {{
            "type": "object",
            "properties": {{
              "filePath": {{
                "type": "string",
                "description": "Path of the file to read."
              }}
            }}
          }}
        }}
      }},
      {{
        "type": "function",
        "function": {{
          "name": "ReadFile",
          "description": "Read a line range of a file. Returns the content of the lines between beginLine and endLine (1-based, inclusive).",
          "parameters": {{
            "type": "object",
            "properties": {{
              "filePath": {{
                "type": "string",
                "description": "Path of the file to read."
              }},
              "beginLine": {{
                "type": "integer",
                "description": "First line number to read (1-based, inclusive)."
              }},
              "endLine": {{
                "type": "integer",
                "description": "Last line number to read (1-based, inclusive)."
              }}
            }}
          }}
        }}
      }},
      {{
        "type": "function",
        "function": {{
          "name": "Move",
          "description": "Move a file or directory to a new location.",
          "parameters": {{
            "type": "object",
            "properties": {{
              "sourcePath": {{
                "type": "string",
                "description": "Path of the file or directory to move"
              }},
              "destinationPath": {{
                "type": "string",
                "description": "Destination path to move the file or directory to"
              }}
            }}
          }}
        }}
      }},
      {{
        "type": "function",
        "function": {{
          "name": "Copy",
          "description": "Copy a file or directory to a new location. The source is preserved and the destination receives a copy.",
          "parameters": {{
            "type": "object",
            "properties": {{
              "sourcePath": {{
                "type": "string",
                "description": "Path of the file or directory to copy"
              }},
              "destinationPath": {{
                "type": "string",
                "description": "Destination path to copy the file or directory to"
              }}
            }}
          }}
        }}
      }},
      {{
        "type": "function",
        "function": {{
          "name": "VcsCreateCommit",
          "description": "Create a commit with the given commit message in the current Jujutsu (jj) repository. The new commit becomes the current top commit.",
          "parameters": {{
            "type": "object",
            "properties": {{
              "commitMessage": {{
                "type": "string",
                "description": "Commit message for the new commit"
              }}
            }}
          }}
        }}
      }},
      {{
        "type": "function",
        "function": {{
          "name": "Bash",
          "description": "Run a whitelisted bash command in a working directory. Returns the combined stdout and stderr of the command.",
          "parameters": {{
            "type": "object",
            "properties": {{
              "bashCommand": {{
                "type": "string",
                "description": "Bash command to execute"
              }},
              "workingDirectory": {{
                "type": "string",
                "description": "Working directory in which to run the command"
              }}
            }}
          }}
        }}
      }}
    ]
  }})", llm_model_);
  request_json_ = nlohmann::json::parse(request_json_str);
  messages_ = request_json_["messages"];

  // When at least one bash command is allowed (read from the settings file's
  // `allowedBashCommands` field), the Bash tool description in the LLM request
  // must include the string representation of those allowed commands. When no
  // bash command is allowed, the Bash tool must not be included in the LLM API
  // request at all, so the LLM is never offered a tool it cannot use.
  const std::vector<AllowedBashCommand>& allowed_bash_commands =
      tool_bash_.allowed_bash_commands();
  auto is_bash_tool = [](const nlohmann::json& tool) {
    return tool.contains("function") && tool["function"].contains("name") &&
        tool["function"]["name"] == "Bash";
  };
  if (allowed_bash_commands.empty()) {
    nlohmann::json filtered_tools = nlohmann::json::array();
    for (nlohmann::json& tool : request_json_["tools"]) {
      if (!is_bash_tool(tool)) {
        filtered_tools.push_back(std::move(tool));
      }
    }
    request_json_["tools"] = std::move(filtered_tools);
  } else {
    nlohmann::json allowed_bash_commands_json = nlohmann::json::array();
    for (const AllowedBashCommand& allowed_command : allowed_bash_commands) {
      allowed_bash_commands_json.push_back({
        {"bashCommand", allowed_command.bash_command},
        {"workingDirectory", allowed_command.working_directory}
      });
    }
    for (nlohmann::json& tool : request_json_["tools"]) {
      if (is_bash_tool(tool)) {
        tool["function"]["description"] =
            "Run a whitelisted bash command in a working directory. Only the "
            "following bash commands are allowed: " +
            allowed_bash_commands_json.dump();
      }
    }
  }
}

void Agent::AppendMessage(const nlohmann::json& message) {
  messages_.push_back(message);
  request_json_["messages"].push_back(message);
  agent_turns_.push_back(AgentTurn(message));
  PersistSessionHistory();
}

void Agent::AppendSlashCommand(const std::string& slash_command) {
  agent_turns_.push_back(AgentTurn(slash_command));
  PersistSessionHistory();
}

void Agent::PersistSessionHistory() {
  const char* home_directory = std::getenv("HOME");
  if (home_directory == nullptr) {
    std::cout << "PersistSessionHistory: Failed to persist session history: HOME is not set."
        << std::endl;
    return;
  }

  std::filesystem::path session_directory =
      std::filesystem::path(home_directory) / ".jiaolong" / "sessions";
  std::error_code error_code;
  std::filesystem::create_directories(session_directory, error_code);
  if (error_code) {
    std::cout << "PersistSessionHistory: Failed to create session directory: " <<
        error_code.message() << std::endl;
    return;
  }

  const std::filesystem::path session_file_path =
      session_directory / ("session_" + session_id_ + ".json");

  nlohmann::json session_history_json;
  session_history_json["creationTimestamp"] = session_creation_timestamp_;
  session_history_json["sessionId"] = session_id_;
  session_history_json["taskId"] = task_id_;
  session_history_json["fileLocation"] = session_file_path.string();
  session_history_json["agentTurns"] = nlohmann::json::array();
  for (const AgentTurn& agent_turn : agent_turns_) {
    session_history_json["agentTurns"].push_back(agent_turn.ToJson());
  }

  std::ofstream session_file(session_file_path);
  session_file << session_history_json.dump(2);
  session_file.close();
}

bool Agent::LoadSessionHistory(const std::string& task_id) {
  const char* home_directory = std::getenv("HOME");
  if (home_directory == nullptr) {
    std::cerr << "LoadSessionHistory: Failed to load session history: HOME is "
        "not set." << std::endl;
    return false;
  }

  const std::filesystem::path session_directory =
      std::filesystem::path(home_directory) / ".jiaolong" / "sessions";
  if (!std::filesystem::is_directory(session_directory)) {
    return false;
  }

  // Scan the sessions folder for the session history JSON file associated
  // with the task id and pick the most recently modified one.
  std::filesystem::path selected_session_file_path;
  std::filesystem::file_time_type selected_file_time{};
  bool has_selected = false;
  for (const auto& entry : std::filesystem::directory_iterator(
      session_directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    std::ifstream session_file(entry.path());
    if (!session_file.is_open()) {
      continue;
    }
    std::stringstream buffer;
    buffer << session_file.rdbuf();
    const nlohmann::json session_history_json =
        nlohmann::json::parse(buffer.str(), nullptr, false);
    if (session_history_json.is_discarded() ||
        !session_history_json.contains("taskId") ||
        session_history_json["taskId"] != task_id) {
      continue;
    }
    if (!has_selected || entry.last_write_time() > selected_file_time) {
      selected_session_file_path = entry.path();
      selected_file_time = entry.last_write_time();
      has_selected = true;
    }
  }
  if (!has_selected) {
    return false;
  }

  std::ifstream session_file(selected_session_file_path);
  if (!session_file.is_open()) {
    return false;
  }
  std::stringstream buffer;
  buffer << session_file.rdbuf();
  const nlohmann::json session_history_json =
      nlohmann::json::parse(buffer.str(), nullptr, false);
  if (session_history_json.is_discarded()) {
    return false;
  }

  // Reset the current state and load the history into it so the agent works
  // as if it had never stopped on the task.
  messages_.clear();
  agent_turns_.clear();
  request_json_["messages"] = nlohmann::json::array();
  if (session_history_json.contains("sessionId") &&
      session_history_json["sessionId"].is_string()) {
    session_id_ = session_history_json["sessionId"].get<std::string>();
  }
  if (session_history_json.contains("creationTimestamp") &&
      session_history_json["creationTimestamp"].is_string()) {
    session_creation_timestamp_ =
        session_history_json["creationTimestamp"].get<std::string>();
  }
  if (session_history_json.contains("agentTurns") &&
      session_history_json["agentTurns"].is_array()) {
    for (const nlohmann::json& turn : session_history_json["agentTurns"]) {
      if (!turn.is_object()) {
        continue;
      }
      if (turn.value("type", "") == "message" &&
          turn.contains("message") && turn["message"].is_object()) {
        const nlohmann::json message = turn["message"];
        messages_.push_back(message);
        request_json_["messages"].push_back(message);
        agent_turns_.push_back(AgentTurn(message));
      } else if (turn.value("type", "") == "slash_command" &&
          turn.contains("command") && turn["command"].is_string()) {
        agent_turns_.push_back(
            AgentTurn(turn["command"].get<std::string>()));
      }
    }
  }

  std::cout << "Resumed session " << session_id_ << " for task " << task_id <<
      " from " << selected_session_file_path.string() << std::endl;

  // Persist the restored state so the session history file reflects the
  // resumed session.
  PersistSessionHistory();
  return true;
}

void Agent::ExecuteToolCalls(const nlohmann::json& tool_calls,
    const std::string& task_working_directory) {
  // When the agent is driven directly (e.g. by unit tests) without going
  // through Work(), no mount table has been configured yet. Fall back to a
  // mount table whose only mount is the implicit `/workspace` mount pointing
  // at the given working directory, matching the pre-feature behavior.
  if (permission_checker_.mount_table().empty()) {
    PermissionContext context;
    context.mount_table =
        MountTable::SingleWorkspaceMount(task_working_directory);
    context.forbidden_to_write = forbidden_to_write_file_list_;
    permission_checker_ = PermissionChecker(std::move(context));
  }

  for (const nlohmann::json& tool_call_json : tool_calls) {
    nlohmann::json tool_message;
    tool_message["role"] = "tool";
    tool_message["content"] = "";

    // Extract the tool call id defensively. A malformed tool call must never
    // crash the agent; when the id is missing or of the wrong type we fall
    // back to an empty id so the tool message is still reported back to the
    // LLM.
    std::string tool_call_id;
    if (tool_call_json.is_object() && tool_call_json.contains("id") &&
        tool_call_json["id"].is_string()) {
      tool_call_id = tool_call_json["id"].get<std::string>();
    }
    tool_message["tool_call_id"] = tool_call_id;

    // A tool call is well-formed only when it carries a `function` object
    // with a string `name` and a string `arguments` payload.
    const nlohmann::json* function_json = nullptr;
    if (tool_call_json.is_object() && tool_call_json.contains("function") &&
        tool_call_json["function"].is_object()) {
      function_json = &tool_call_json["function"];
    }

    std::string tool_function_name;
    std::string args_str;
    bool tool_call_well_formed = function_json != nullptr;
    if (tool_call_well_formed) {
      tool_call_well_formed = TryReadStringArg(
          tool_message, *function_json, "name", tool_function_name);
    }
    if (tool_call_well_formed) {
      tool_call_well_formed = TryReadStringArg(
          tool_message, *function_json, "arguments", args_str);
    }
    if (!tool_call_well_formed) {
      if (tool_message["content"].get<std::string>().empty()) {
        tool_message["content"] =
            "invalid input, error_message: malformed tool call.";
      }
      std::cout << "Skipping malformed tool call: " <<
          tool_call_json.dump(-1, ' ', false,
              nlohmann::json::error_handler_t::replace) << std::endl;
      AppendMessage(tool_message);
      std::cout << "[tool] " << tool_message.dump(-1, ' ', false,
          nlohmann::json::error_handler_t::replace) << std::endl;
      continue;
    }

    // Parse the tool call arguments without relying on exceptions.
    nlohmann::json args_json = nlohmann::json::parse(args_str, nullptr, false);
    const bool args_parsed = !args_json.is_discarded();
    std::cout << "[tool] function: " << tool_function_name << ", args: " <<
        args_str << ", tool_call_id: " << tool_call_id << std::endl;

    // Ask user for tool call approval.
    bool tool_call_approved = true;
    std::string user_approval_decision;
    if (tool_permission_ == ToolPermission::kAsk) {
      std::cout << "Do you approve this tool call (y / enter rejection reason)? ";
      std::getline(std::cin, user_approval_decision);
      tool_call_approved = (user_approval_decision == "y" ||
          user_approval_decision == "Y");
    }

    if (tool_call_approved) {
      if (!args_parsed) {
        tool_message["content"] =
            "invalid input, error_message: failed to parse tool call arguments.";
        std::cout << "Failed to parse tool call arguments: " << args_str <<
            std::endl;
      } else if (tool_function_name == "SearchAndReplace") {
        ExecuteToolSearchAndReplace(tool_message, args_json,
            task_working_directory);
      } else if (tool_function_name == "CreateNewFile") {
        ExecuteToolCreateNewFile(tool_message, args_json,
            task_working_directory);
      } else if (tool_function_name == "CreateNewDirectory") {
        ExecuteToolCreateNewDirectory(tool_message, args_json,
            task_working_directory);
      } else if (tool_function_name == "WriteContentToFile") {
        ExecuteToolWriteContentToFile(tool_message, args_json,
            task_working_directory);
      } else if (tool_function_name == "Delete") {
        ExecuteToolDelete(tool_message, args_json,
            task_working_directory);
      } else if (tool_function_name == "FindFiles") {
        ExecuteToolFindFiles(tool_message, args_json, task_working_directory);
      } else if (tool_function_name == "FindStrings") {
        ExecuteToolFindStrings(tool_message, args_json,
            task_working_directory);
      } else if (tool_function_name == "ListDirectory") {
        ExecuteToolListDirectory(tool_message, args_json,
            task_working_directory);
      } else if (tool_function_name == "ReadFile") {
        ExecuteToolReadFile(tool_message, args_json,
            task_working_directory);
      } else if (tool_function_name == "ReadWholeFile") {
        ExecuteToolReadWholeFile(tool_message, args_json,
            task_working_directory);
      } else if (tool_function_name == "Move") {
        ExecuteToolMove(tool_message, args_json,
            task_working_directory);
      } else if (tool_function_name == "Copy") {
        ExecuteToolCopy(tool_message, args_json,
            task_working_directory);
      } else if (tool_function_name == "VcsCreateCommit") {
        ExecuteToolVcsCreateCommit(tool_message, args_json,
            task_working_directory);
      } else if (tool_function_name == "Bash") {
        ExecuteToolBash(tool_message, args_json,
            task_working_directory);
      } else {
        tool_message["content"] = "Illegal tool function name.";
        std::cout << "Illegal tool function name: " << tool_function_name <<
            std::endl;
      }
    } else {
      tool_message["content"] = "user rejected tool call, reason: " +
          user_approval_decision;
      std::cout << "You rejected the tool call with reason: " <<
          user_approval_decision << std::endl;
    }

    // Append the tool message
    AppendMessage(tool_message);

    // Print tool use. Use error_handler_t::replace so that non-UTF-8
    // content (e.g. from a file that slipped through as binary) is replaced
    // instead of crashing the agent.
    std::cout << "[tool] " << tool_message.dump(-1, ' ', false,
        nlohmann::json::error_handler_t::replace) << std::endl;
  }
}

void Agent::ShowLlmLoading() {
  // If it's already loading, do nothing
  if (is_llm_loading_) {
    return;
  }

  is_llm_loading_ = true;

  // Start a background thread for the animation
  show_loading_thread_ = std::thread([&]() {
    const std::vector<std::string> frames = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    int i = 0;

    while (is_llm_loading_) {
      // \r moves the cursor to the beginning of the line
      // std::flush ensures the output is printed immediately
      std::cout << "\r" << frames[i] << " [" << llm_model_ << "] " << llm_loading_message_ << std::flush;

      i = (i + 1) % 10;

      // Sleep for a short duration to control animation speed
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
  });
}

void Agent::DismissLlmLoading() {
  // If it's not loading, do nothing
  if (!is_llm_loading_) {
    return;
  }

  // Signal the background thread to stop
  is_llm_loading_ = false;

  // Wait for the thread to finish
  if (show_loading_thread_.joinable()) {
      show_loading_thread_.join();
  }

  // Clear the line in stdout
  // \r goes to start, print spaces to overwrite the spinner, \r goes back to start again
  std::cout << "\r" << std::string(llm_loading_message_.length() + 2, ' ') << "\r" << std::flush;
}

nlohmann::json Agent::ConstructNewUserMessage(const Task& task) {
  nlohmann::json user_message;
  user_message["role"] = "user";
  std::string content_str = std::format("Task title: `{}`.\n"
    "Task description: `{}`.\n"
    "Task ID: `{}`.\n"
    "Working directory: `/workspace`.\n"
    "Rationale: Check whether task is completed. If not, use available tools to complete it. Generate all needed tool calls at once. Do not make any irrelevant changes. If completed, stop directly. After you have completes the task by modifying the files in the workspace, you should call `VcsCreateCommit` to create a commit as the final action of this session. The commit message content should be a summary title, and a line to show the associated task, like `SUMMARY_TITLE\\n\\nTask: TASK_ID`. Do not check git/jj binaries or folders, since the tool already encapsulates all the implementation and authentication details for you.\n"
    "Tools: You can only use available tools. Do not even try to build or test, and you have no available tools for these operations.",
    task.title_, task.description_, task.id_);
  user_message["content"] = content_str;
  return user_message;
}

float Agent::CalculateAverageLlmResponseDurationInSecond() {
  const int num_responses = llm_response_durations_in_second_.size();
  int duration_sum = 0;
  for (const int duration : llm_response_durations_in_second_) {
    duration_sum += duration;
  }
  return (float) duration_sum / num_responses;
}

void Agent::ExecuteToolSearchAndReplace(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {

  // Convert the virtual path to real path (absolute)
  std::string virtual_file_path;
  if (!TryReadStringArg(tool_message, args_json, "filePath",
      virtual_file_path)) {
    return;
  }
  const ResolvedPathAccess path_access = permission_checker_.ResolvePath(
      virtual_file_path, Tool::PermissionType::kWrite);
  if (path_access.status != PathAccessStatus::kOk) {
    tool_message["content"] =
        path_access.status == PathAccessStatus::kForbiddenToWrite
        ? "invalid input, error_message: forbidden file path."
        : "invalid input, error_message: invalid file path.";
    return;
  }
  const std::string real_file_path = path_access.real_path.string();

  if (IsForbiddenToWrite(tool_search_and_replace_, real_file_path)) {
    tool_message["content"] =
        "invalid input, error_message: forbidden file path.";
    return;
  }

  std::string old_str;
  if (!TryReadStringArg(tool_message, args_json, "oldStr", old_str)) {
    return;
  }
  std::string new_str;
  if (!TryReadStringArg(tool_message, args_json, "newStr", new_str)) {
    return;
  }
  auto tool_use_status_and_output = tool_search_and_replace_.SearchAndReplace(
      ToolEditSearchAndReplaceInput(real_file_path, old_str, new_str));
  std::shared_ptr<ToolUseStatus> tool_use_status = tool_use_status_and_output.first;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess:
      tool_message["content"] = "success";
      break;
    case ToolUseStatusType::kInvalidInput:
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    case ToolUseStatusType::kExecutionFailure:
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
  }
}

void Agent::ExecuteToolCreateNewFile(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {
  std::string virtual_file_path;
  if (!TryReadStringArg(tool_message, args_json, "filePath",
      virtual_file_path)) {
    return;
  }
  const ResolvedPathAccess path_access = permission_checker_.ResolvePath(
      virtual_file_path, Tool::PermissionType::kWrite);
  if (path_access.status != PathAccessStatus::kOk) {
    tool_message["content"] =
        path_access.status == PathAccessStatus::kForbiddenToWrite
        ? "invalid input, error_message: forbidden file path."
        : "invalid input, error_message: invalid file path.";
    return;
  }
  const std::string real_file_path = path_access.real_path.string();

  if (IsForbiddenToWrite(tool_create_new_file_, real_file_path)) {
    tool_message["content"] =
        "invalid input, error_message: forbidden file path.";
    return;
  }
  std::string initial_content;
  if (!TryReadStringArg(tool_message, args_json, "initialContent",
      initial_content)) {
    return;
  }
  auto tool_use_status_and_output = tool_create_new_file_.CreateNewFile(
      ToolEditCreateNewFileInput(real_file_path, initial_content));
  std::shared_ptr<ToolUseStatus> tool_use_status = tool_use_status_and_output.first;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess:
      tool_message["content"] = "success";
      break;
    case ToolUseStatusType::kInvalidInput:
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    case ToolUseStatusType::kExecutionFailure:
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
  }
}

void Agent::ExecuteToolCreateNewDirectory(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {
  std::string virtual_path;
  if (!TryReadStringArg(tool_message, args_json, "directoryPath",
      virtual_path)) {
    return;
  }
  const ResolvedPathAccess path_access = permission_checker_.ResolvePath(
      virtual_path, Tool::PermissionType::kWrite);
  if (path_access.status != PathAccessStatus::kOk) {
    tool_message["content"] =
        path_access.status == PathAccessStatus::kForbiddenToWrite
        ? "invalid input, error_message: forbidden directory path."
        : "invalid input, error_message: invalie directory path.";
    return;
  }
  const std::string real_path = path_access.real_path.string();

  if (IsForbiddenToWrite(tool_create_new_directory_, real_path)) {
    tool_message["content"] =
        "invalid input, error_message: forbidden directory path.";
    return;
  }
  auto tool_use_status_and_output = tool_create_new_directory_.CreateNewDirectory(
      ToolEditCreateNewDirectoryInput(real_path));
  std::shared_ptr<ToolUseStatus> tool_use_status = tool_use_status_and_output.first;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess:
      tool_message["content"] = "success";
      break;
    case ToolUseStatusType::kInvalidInput:
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    case ToolUseStatusType::kExecutionFailure:
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
  }
}

void Agent::ExecuteToolWriteContentToFile(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {
  std::string virtual_file_path;
  if (!TryReadStringArg(tool_message, args_json, "filePath",
      virtual_file_path)) {
    return;
  }
  const ResolvedPathAccess path_access = permission_checker_.ResolvePath(
      virtual_file_path, Tool::PermissionType::kWrite);
  if (path_access.status != PathAccessStatus::kOk) {
    tool_message["content"] =
        path_access.status == PathAccessStatus::kForbiddenToWrite
        ? "invalid input, error_message: forbidden file path."
        : "invalid input, error_message: invalid file path.";
    return;
  }
  const std::string real_file_path = path_access.real_path.string();

  if (IsForbiddenToWrite(tool_write_content_to_file_, real_file_path)) {
    tool_message["content"] =
        "invalid input, error_message: forbidden file path.";
    return;
  }
  std::string content;
  if (!TryReadStringArg(tool_message, args_json, "content", content)) {
    return;
  }
  auto tool_use_status_and_output = tool_write_content_to_file_.WriteContentToFile(
      ToolEditWriteContentToFileInput(real_file_path, content));
  std::shared_ptr<ToolUseStatus> tool_use_status = tool_use_status_and_output.first;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess:
      tool_message["content"] = "success";
      break;
    case ToolUseStatusType::kInvalidInput:
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    case ToolUseStatusType::kExecutionFailure:
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
  }
}

void Agent::ExecuteToolDelete(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {
  std::string virtual_path;
  if (!TryReadStringArg(tool_message, args_json, "path", virtual_path)) {
    return;
  }
  const ResolvedPathAccess path_access = permission_checker_.ResolvePath(
      virtual_path, Tool::PermissionType::kWrite);
  if (path_access.status != PathAccessStatus::kOk) {
    tool_message["content"] =
        path_access.status == PathAccessStatus::kForbiddenToWrite
        ? "invalid input, error_message: forbidden path."
        : "invalid input, error_message: invalid path.";
    return;
  }
  const std::string real_path = path_access.real_path.string();

  if (IsForbiddenToWrite(tool_delete_, real_path)) {
    tool_message["content"] = "invalid input, error_message: forbidden path.";
    return;
  }
  auto tool_use_status_and_output = tool_delete_.Delete(
      ToolDeleteInput(real_path));
  std::shared_ptr<ToolUseStatus> tool_use_status = tool_use_status_and_output.first;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess:
      tool_message["content"] = "success";
      break;
    case ToolUseStatusType::kInvalidInput:
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    case ToolUseStatusType::kExecutionFailure:
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
  }
}

void Agent::ExecuteToolFindFiles(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {
  std::string virtual_root_path;
  if (!TryReadStringArg(tool_message, args_json, "rootPath",
      virtual_root_path)) {
    return;
  }
  const ResolvedPathAccess path_access = permission_checker_.ResolvePath(
      virtual_root_path, Tool::PermissionType::kRead);
  if (path_access.status != PathAccessStatus::kOk) {
    tool_message["content"] =
        path_access.status == PathAccessStatus::kForbiddenToWrite
        ? "invalid input, error_message: forbidden root path."
        : "invalid input, error_message: invalid root path.";
    return;
  }
  const std::string real_root_path = path_access.real_path.string();

  if (IsForbiddenToWrite(tool_find_files_, real_root_path)) {
    tool_message["content"] =
        "invalid input, error_message: forbidden root path.";
    return;
  }
  std::string query;
  if (!TryReadStringArg(tool_message, args_json, "query", query)) {
    return;
  }
  auto tool_use_status_and_output = tool_find_files_.FindFiles(
      ToolFindFindFilesInput(real_root_path, query));
  std::shared_ptr<ToolUseStatus> tool_use_status = tool_use_status_and_output.first;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess: {
      // Serialize the result as JSON.
      nlohmann::json result_json;
      result_json["presentFiles"] = nlohmann::json::array();
      for (const std::string& real_file_path :
          tool_use_status_and_output.second.present_files_) {
        const std::optional<std::string> virtual_file_path =
            permission_checker_.mount_table().ToVirtualPath(real_file_path);
        if (!virtual_file_path.has_value()) {
          continue;
        }
        result_json["presentFiles"].push_back(
            {{"filePath", *virtual_file_path}});
      }
      tool_message["content"] = result_json.dump();
      break;
    }
    case ToolUseStatusType::kInvalidInput:
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    case ToolUseStatusType::kExecutionFailure:
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
  }
}

void Agent::ExecuteToolFindStrings(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {
  std::string virtual_root_path;
  if (!TryReadStringArg(tool_message, args_json, "rootPath",
      virtual_root_path)) {
    return;
  }
  const ResolvedPathAccess path_access = permission_checker_.ResolvePath(
      virtual_root_path, Tool::PermissionType::kRead);
  if (path_access.status != PathAccessStatus::kOk) {
    tool_message["content"] =
        path_access.status == PathAccessStatus::kForbiddenToWrite
        ? "invalid input, error_message: forbidden root path."
        : "invalid input, error_message: invalid root path.";
    return;
  }
  const std::string real_root_path = path_access.real_path.string();

  if (IsForbiddenToWrite(tool_find_strings_, real_root_path)) {
    tool_message["content"] =
        "invalid input, error_message: forbidden root path.";
    return;
  }
  std::string query;
  if (!TryReadStringArg(tool_message, args_json, "query", query)) {
    return;
  }
  auto tool_use_status_and_output = tool_find_strings_.FindStrings(
      ToolFindFindStringsInput(real_root_path, query));
  std::shared_ptr<ToolUseStatus> tool_use_status = tool_use_status_and_output.first;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess: {
      // Serialize the result as JSON.
      nlohmann::json result_json;
      result_json["presentFiles"] = nlohmann::json::array();
      for (const ToolFindFileWithLineNumbers& present_file :
          tool_use_status_and_output.second.present_files_) {
        const std::optional<std::string> virtual_file_path =
            permission_checker_.mount_table().ToVirtualPath(
                present_file.file_path_);
        if (!virtual_file_path.has_value()) {
          continue;
        }
        result_json["presentFiles"].push_back({
          {"filePath", *virtual_file_path},
          {"presentLineNumbers", present_file.present_line_numbers_}
        });
      }
      tool_message["content"] = result_json.dump();
      break;
    }
    case ToolUseStatusType::kInvalidInput:
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    case ToolUseStatusType::kExecutionFailure:
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
  }
}

void Agent::ExecuteToolListDirectory(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {
  std::string virtual_directory_path;
  if (!TryReadStringArg(tool_message, args_json, "directoryPath",
      virtual_directory_path)) {
    return;
  }
  const ResolvedPathAccess path_access = permission_checker_.ResolvePath(
      virtual_directory_path, Tool::PermissionType::kRead);
  if (path_access.status != PathAccessStatus::kOk) {
    tool_message["content"] =
        path_access.status == PathAccessStatus::kForbiddenToWrite
        ? "invalid input, error_message: forbidden directory path."
        : "invalid input, error_message: invalid directory path.";
    return;
  }
  const std::string real_directory_path = path_access.real_path.string();

  if (IsForbiddenToWrite(tool_list_directory_, real_directory_path)) {
    tool_message["content"] =
        "invalid input, error_message: forbidden directory path.";
    return;
  }
  auto tool_use_status_and_output = tool_list_directory_.ListDirectory(
      ToolListDirectoryInput(real_directory_path));
  std::shared_ptr<ToolUseStatus> tool_use_status = tool_use_status_and_output.first;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess: {
      // Serialize the result as JSON.
      nlohmann::json result_json;
      result_json["entries"] = nlohmann::json::array();
      for (const ToolListDirectoryEntry& entry :
          tool_use_status_and_output.second.entries_) {
        result_json["entries"].push_back({
          {"name", entry.name_},
          {"isDirectory", entry.is_directory_}
        });
      }
      tool_message["content"] = result_json.dump();
      break;
    }
    case ToolUseStatusType::kInvalidInput:
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    case ToolUseStatusType::kExecutionFailure:
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
  }
}

void Agent::ExecuteToolReadWholeFile(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {
  std::string virtual_file_path;
  if (!TryReadStringArg(tool_message, args_json, "filePath",
      virtual_file_path)) {
    return;
  }
  const ResolvedPathAccess path_access = permission_checker_.ResolvePath(
      virtual_file_path, Tool::PermissionType::kRead);
  if (path_access.status != PathAccessStatus::kOk) {
    tool_message["content"] =
        path_access.status == PathAccessStatus::kForbiddenToWrite
        ? "invalid input, error_message: forbidden file path."
        : "invalid input, error_message: invalid file path.";
    return;
  }
  const std::string real_file_path = path_access.real_path.string();

  if (IsForbiddenToWrite(tool_read_whole_file_, real_file_path)) {
    tool_message["content"] =
        "invalid input, error_message: forbidden file path.";
    return;
  }
  auto tool_use_status_and_output = tool_read_whole_file_.ReadWholeFile(
      ToolReadReadWholeFileInput(real_file_path));
  auto tool_use_status = tool_use_status_and_output.first;
  auto& tool_output = tool_use_status_and_output.second;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess: {
      nlohmann::json result_json;
      result_json["fileContent"] = tool_output.file_content_;
      tool_message["content"] = result_json.dump();
      break;
    }
    case ToolUseStatusType::kInvalidInput: {
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    }
    case ToolUseStatusType::kExecutionFailure: {
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
    }
  }
}

void Agent::ExecuteToolReadFile(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {
  std::string virtual_file_path;
  if (!TryReadStringArg(tool_message, args_json, "filePath",
      virtual_file_path)) {
    return;
  }
  const ResolvedPathAccess path_access = permission_checker_.ResolvePath(
      virtual_file_path, Tool::PermissionType::kRead);
  if (path_access.status != PathAccessStatus::kOk) {
    tool_message["content"] =
        path_access.status == PathAccessStatus::kForbiddenToWrite
        ? "invalid input, error_message: forbidden file path."
        : "invalid input, error_message: invalid file path.";
    return;
  }
  const std::string real_file_path = path_access.real_path.string();

  if (IsForbiddenToWrite(tool_read_file_of_line_range_, real_file_path)) {
    tool_message["content"] =
        "invalid input, error_message: forbidden file path.";
    return;
  }
  int begin_line = 0;
  if (!TryReadIntArg(tool_message, args_json, "beginLine", begin_line)) {
    return;
  }
  int end_line = 0;
  if (!TryReadIntArg(tool_message, args_json, "endLine", end_line)) {
    return;
  }
  auto tool_use_status_and_output = tool_read_file_of_line_range_.ReadFileOfLineRange(
      ToolReadReadFileOfLineRangeInput(real_file_path, begin_line, end_line));
  auto tool_use_status = tool_use_status_and_output.first;
  auto& tool_output = tool_use_status_and_output.second;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess: {
      nlohmann::json result_json;
      result_json["fileContent"] = tool_output.file_content_;
      tool_message["content"] = result_json.dump();
      break;
    }
    case ToolUseStatusType::kInvalidInput: {
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    }
    case ToolUseStatusType::kExecutionFailure: {
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
    }
  }
}

void Agent::ExecuteToolMove(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {
  std::string virtual_source_path;
  if (!TryReadStringArg(tool_message, args_json, "sourcePath",
      virtual_source_path)) {
    return;
  }
  std::string virtual_destination_path;
  if (!TryReadStringArg(tool_message, args_json, "destinationPath",
      virtual_destination_path)) {
    return;
  }
  const ResolvedPathAccess source_access = permission_checker_.ResolvePath(
      virtual_source_path, Tool::PermissionType::kWrite);
  const ResolvedPathAccess destination_access = permission_checker_.ResolvePath(
      virtual_destination_path, Tool::PermissionType::kWrite);
  if (source_access.status != PathAccessStatus::kOk ||
      destination_access.status != PathAccessStatus::kOk) {
    tool_message["content"] =
        (source_access.status == PathAccessStatus::kForbiddenToWrite ||
         destination_access.status == PathAccessStatus::kForbiddenToWrite)
        ? "invalid input, error_message: forbidden path."
        : "invalid input, error_message: invalid path.";
    return;
  }
  const std::string real_source_path = source_access.real_path.string();
  const std::string real_destination_path =
      destination_access.real_path.string();

  if (IsForbiddenToWrite(tool_move_, real_source_path) ||
      IsForbiddenToWrite(tool_move_, real_destination_path)) {
    tool_message["content"] = "invalid input, error_message: forbidden path.";
    return;
  }
  auto tool_use_status_and_output = tool_move_.Move(
      ToolMoveInput(real_source_path, real_destination_path));
  std::shared_ptr<ToolUseStatus> tool_use_status = tool_use_status_and_output.first;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess:
      tool_message["content"] = "success";
      break;
    case ToolUseStatusType::kInvalidInput:
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    case ToolUseStatusType::kExecutionFailure:
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
  }
}

void Agent::ExecuteToolCopy(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {
  std::string virtual_source_path;
  if (!TryReadStringArg(tool_message, args_json, "sourcePath",
      virtual_source_path)) {
    return;
  }
  std::string virtual_destination_path;
  if (!TryReadStringArg(tool_message, args_json, "destinationPath",
      virtual_destination_path)) {
    return;
  }
  const ResolvedPathAccess source_access = permission_checker_.ResolvePath(
      virtual_source_path, Tool::PermissionType::kRead);
  const ResolvedPathAccess destination_access = permission_checker_.ResolvePath(
      virtual_destination_path, Tool::PermissionType::kWrite);
  if (source_access.status != PathAccessStatus::kOk ||
      destination_access.status != PathAccessStatus::kOk) {
    tool_message["content"] =
        (source_access.status == PathAccessStatus::kForbiddenToWrite ||
         destination_access.status == PathAccessStatus::kForbiddenToWrite)
        ? "invalid input, error_message: forbidden path."
        : "invalid input, error_message: invalid path.";
    return;
  }
  const std::string real_source_path = source_access.real_path.string();
  const std::string real_destination_path =
      destination_access.real_path.string();

  if (IsForbiddenToWrite(tool_copy_, real_destination_path)) {
    tool_message["content"] = "invalid input, error_message: forbidden path.";
    return;
  }
  auto tool_use_status_and_output = tool_copy_.Copy(
      ToolCopyInput(real_source_path, real_destination_path));
  std::shared_ptr<ToolUseStatus> tool_use_status = tool_use_status_and_output.first;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess:
      tool_message["content"] = "success";
      break;
    case ToolUseStatusType::kInvalidInput:
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    case ToolUseStatusType::kExecutionFailure:
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
  }
}

void Agent::ExecuteToolVcsCreateCommit(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {
  std::string commit_message;
  if (!TryReadStringArg(tool_message, args_json, "commitMessage",
      commit_message)) {
    return;
  }
  auto tool_use_status_and_output = tool_vcs_create_commit_.CreateCommit(
      ToolVcsCreateCommitInput(commit_message, task_working_directory));
  std::shared_ptr<ToolUseStatus> tool_use_status = tool_use_status_and_output.first;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess: {
      nlohmann::json result_json;
      result_json["stdout"] = tool_use_status_and_output.second.stdout_output_;
      tool_message["content"] = result_json.dump();
      break;
    }
    case ToolUseStatusType::kInvalidInput:
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    case ToolUseStatusType::kExecutionFailure:
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
  }
}

void Agent::ExecuteToolBash(nlohmann::json& tool_message,
    const nlohmann::json& args_json,
    const std::string& task_working_directory) {
  std::string bash_command;
  if (!TryReadStringArg(tool_message, args_json, "bashCommand",
      bash_command)) {
    return;
  }
  std::string working_directory;
  if (!TryReadStringArg(tool_message, args_json, "workingDirectory",
      working_directory)) {
    return;
  }

  // For Jiaolong, the Bash tool is gated exclusively by the settings-file
  // whitelist (`allowedBashCommands`): a command runs only when both the
  // command text and the working directory match an allowed entry exactly.
  // The mount-table permission checker must not restrict the Bash working
  // directory, so it is deliberately not consulted here.
  auto tool_use_status_and_output = tool_bash_.Execute(
      ToolBashInput(bash_command, working_directory));
  std::shared_ptr<ToolUseStatus> tool_use_status = tool_use_status_and_output.first;
  switch (tool_use_status->type_) {
    case ToolUseStatusType::kSuccess: {
      nlohmann::json result_json;
      result_json["stdout"] = tool_use_status_and_output.second.stdout_output_;
      tool_message["content"] = result_json.dump();
      break;
    }
    case ToolUseStatusType::kInvalidInput:
      tool_message["content"] = "invalid input, error_message: " +
          static_pointer_cast<ToolUseInvalidInput>(tool_use_status)->error_message_;
      break;
    case ToolUseStatusType::kExecutionFailure:
      tool_message["content"] = "execution failure, error_message: " +
          static_pointer_cast<ToolUseExecutionFailure>(tool_use_status)->error_message_;
      break;
  }
}

void Agent::SetToolPermission(ToolPermission tool_permission) {
  tool_permission_ = tool_permission;
}

}  // namsepace jiaolong
