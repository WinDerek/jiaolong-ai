#include <iostream>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "agent/permission_checker/claude_permission_checker.h"
#include "agent/permission_checker/path_utils.h"
#include "util/json_utils.h"

namespace jiaolong {

ClaudePermissionChecker::ClaudePermissionChecker() : bash_command_permission_checker_(allowed_target_paths_) {
  allowed_working_directories_.emplace_back("/home/user/workspace");

  allowed_target_paths_.emplace_back("/home/user/workspace/code");
  allowed_target_paths_.emplace_back("/home/user/workspace/doc");

  allowed_tools_.insert("Bash");
  allowed_tools_.insert("Edit");
  allowed_tools_.insert("Write");
  allowed_tools_.insert("Read");
  allowed_tools_.insert("Glob");
  allowed_tools_.insert("Grep");
}

PermissionCheckResult ClaudePermissionChecker::CheckPermission(
    const nlohmann::json& parameters_json) {
  PermissionCheckResult result;

  // Check whether the working directory is in one of the allowed paths
  std::filesystem::path working_directory_path(
      RequireStringJsonField(parameters_json, "cwd"));
  result.allowed = IsPathAllowed(working_directory_path,
      allowed_working_directories_);
  if (!result.allowed) {
    result.reason = "working directory not allowed: \"" +
    working_directory_path.string() + "\"";
    return result;
  }

  // Check whether the tool is allowed
  const std::string tool_name =
      RequireStringJsonField(parameters_json, "tool_name");
  result.allowed &= (allowed_tools_.contains(tool_name));
  if (!result.allowed) {
    result.reason = "tool not allowed: \"" + tool_name + "\"";
    return result;
  }

  // Check tool input parameters of allowed tools
  const nlohmann::json& tool_input_json =
      RequireJsonField(parameters_json, "tool_input");
  if (tool_name == "Bash") {
    return bash_command_permission_checker_.CheckCommand(
        RequireStringJsonField(tool_input_json, "command"),
        working_directory_path);
  } else if (tool_name == "Edit" || tool_name == "Write"
      || tool_name == "Read") {
    const std::filesystem::path file_path(
        RequireStringJsonField(tool_input_json, "file_path"));
    result.allowed &= IsPathAllowed(file_path, allowed_target_paths_);
    if (!result.allowed) {
      result.reason = "file path not allowed: \"" + file_path.string() + "\"";
      return result;
    }
  } else if (tool_name == "Glob" || tool_name == "Grep") {
    const std::filesystem::path path(
        RequireStringJsonField(tool_input_json, "path"));
    result.allowed &= IsPathAllowed(path, allowed_target_paths_);
    if (!result.allowed) {
      result.reason = "search path not allowed: \"" + path.string() + "\"";
      return result;
    }
  }

  return result;
}

}  // namsepace jiaolong
