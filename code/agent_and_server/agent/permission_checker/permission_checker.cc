#include "agent/permission_checker/permission_checker.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace jiaolong {

namespace {

// Describes how a tool call's path arguments must be checked: each entry is a
// (argument field name, permission type) pair, in the order the fields must be
// checked. `VcsCreateCommit` has no path argument.
struct ToolPathSpec {
  std::vector<std::pair<std::string, Tool::PermissionType>> paths;
};

// Returns the path-argument spec for a tool function, or std::nullopt when the
// function is unknown to the agent.
std::optional<ToolPathSpec> LookupToolPathSpec(const std::string& function_name) {
  if (function_name == "SearchAndReplace") {
    return ToolPathSpec{{{"filePath", Tool::PermissionType::kWrite}}};
  }
  if (function_name == "CreateNewFile") {
    return ToolPathSpec{{{"filePath", Tool::PermissionType::kWrite}}};
  }
  if (function_name == "CreateNewDirectory") {
    return ToolPathSpec{{{"directoryPath", Tool::PermissionType::kWrite}}};
  }
  if (function_name == "WriteContentToFile") {
    return ToolPathSpec{{{"filePath", Tool::PermissionType::kWrite}}};
  }
  if (function_name == "Delete") {
    return ToolPathSpec{{{"path", Tool::PermissionType::kWrite}}};
  }
  if (function_name == "FindFiles") {
    return ToolPathSpec{{{"rootPath", Tool::PermissionType::kRead}}};
  }
  if (function_name == "FindStrings") {
    return ToolPathSpec{{{"rootPath", Tool::PermissionType::kRead}}};
  }
  if (function_name == "ListDirectory") {
    return ToolPathSpec{{{"directoryPath", Tool::PermissionType::kRead}}};
  }
  if (function_name == "ReadWholeFile") {
    return ToolPathSpec{{{"filePath", Tool::PermissionType::kRead}}};
  }
  if (function_name == "ReadFile") {
    return ToolPathSpec{{{"filePath", Tool::PermissionType::kRead}}};
  }
  if (function_name == "Move") {
    // Move cannot delete from a readonly mount, so both ends must be RW.
    return ToolPathSpec{{{"sourcePath", Tool::PermissionType::kWrite},
                         {"destinationPath", Tool::PermissionType::kWrite}}};
  }
  if (function_name == "Copy") {
    // Copy is the cross-mount bridge: the source may be RO, the destination
    // must be RW.
    return ToolPathSpec{{{"sourcePath", Tool::PermissionType::kRead},
                         {"destinationPath", Tool::PermissionType::kWrite}}};
  }
  if (function_name == "VcsCreateCommit") {
    return ToolPathSpec{};
  }
  // `Bash` is intentionally absent: for the Jiaolong Agent a bash command is
  // gated exclusively by the settings-file whitelist (`allowedBashCommands`),
  // not by the mount-table permission checker.
  return std::nullopt;
}

}  // namespace

PermissionChecker::PermissionChecker() = default;

PermissionChecker::PermissionChecker(PermissionContext context)
    : context_(std::move(context)) {}

bool PermissionChecker::IsForbiddenToWrite(const std::string& real_path) const {
  for (const std::string& forbidden_path : context_.forbidden_to_write) {
    if (forbidden_path == real_path) {
      return true;
    }
  }
  return false;
}

ResolvedPathAccess PermissionChecker::ResolvePath(
    const std::string& virtual_path,
    Tool::PermissionType permission_type) const {
  ResolvedPathAccess result;
  const std::optional<ResolvedPath> resolved =
      context_.mount_table.ToRealPath(virtual_path);
  if (!resolved.has_value()) {
    result.status = PathAccessStatus::kInvalidPath;
    return result;
  }
  // Write tools may only touch RW mounts; read tools may touch any mount.
  if (permission_type == Tool::PermissionType::kWrite &&
      resolved->access == MountAccess::kReadOnly) {
    result.status = PathAccessStatus::kForbiddenToWrite;
    return result;
  }
  result.status = PathAccessStatus::kOk;
  result.real_path = resolved->real_path;
  result.access = resolved->access;
  return result;
}

PermissionCheckResult PermissionChecker::CheckToolCall(
    const std::string& function_name, const nlohmann::json& args) const {
  PermissionCheckResult result;
  const std::optional<ToolPathSpec> spec = LookupToolPathSpec(function_name);
  if (!spec.has_value()) {
    result.allowed = false;
    result.reason = "unknown tool function: " + function_name;
    return result;
  }

  for (const std::pair<std::string, Tool::PermissionType>& path_field :
      spec->paths) {
    const std::string& field = path_field.first;
    const Tool::PermissionType permission_type = path_field.second;
    if (!args.is_object() || !args.contains(field) ||
        !args[field].is_string()) {
      result.allowed = false;
      result.reason = "missing or invalid path argument: " + field;
      return result;
    }
    const std::string virtual_path = args[field].get<std::string>();
    const ResolvedPathAccess resolved =
        ResolvePath(virtual_path, permission_type);
    if (resolved.status == PathAccessStatus::kInvalidPath) {
      result.allowed = false;
      result.reason = "invalid path";
      return result;
    }
    if (resolved.status == PathAccessStatus::kForbiddenToWrite) {
      result.allowed = false;
      result.reason = "forbidden file path";
      return result;
    }
    if (permission_type == Tool::PermissionType::kWrite &&
        IsForbiddenToWrite(resolved.real_path.string())) {
      result.allowed = false;
      result.reason = "forbidden file path";
      return result;
    }
    result.real_paths.push_back(resolved.real_path.string());
  }

  result.allowed = true;
  result.reason = "";
  return result;
}

}  // namespace jiaolong