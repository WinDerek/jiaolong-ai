#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent/permission_checker/mount_table.h"
#include "agent/permission_checker/permission_check_result.h"
#include "agent/tool/tool.h"

namespace jiaolong {

// The context a PermissionChecker needs to decide whether a tool call is
// allowed: the task's mount table plus the list of real file paths the agent
// is forbidden to write to.
struct PermissionContext {
  MountTable mount_table;
  std::vector<std::string> forbidden_to_write;
};

// Outcome of resolving a single virtual path argument against the mount table
// for a tool with a given permission type.
enum class PathAccessStatus {
  // The path resolved to a real path the tool may access.
  kOk,
  // The path is not under any mount, or it escapes its mount root.
  kInvalidPath,
  // A write tool tried to touch a read-only mount, or the resolved real path
  // is on the forbidden-to-write list.
  kForbiddenToWrite,
};

struct ResolvedPathAccess {
  PathAccessStatus status = PathAccessStatus::kInvalidPath;
  std::filesystem::path real_path;
  MountAccess access = MountAccess::kReadWrite;
};

// Centralizes the path and access-mode checks every tool call must pass. It
// resolves virtual paths through the task's mount table, enforces the
// read/write boundary (read tools may touch any mount; write tools may touch
// only RW mounts) and applies the forbidden-to-write file list to write tools.
class PermissionChecker {

 public:

  PermissionChecker();

  explicit PermissionChecker(PermissionContext context);

  // Returns allowed=true plus the resolved real path(s), or allowed=false plus
  // a reason the agent turns into the tool message content.
  PermissionCheckResult CheckToolCall(const std::string& function_name,
      const nlohmann::json& args) const;

  // Resolves one virtual path argument for a tool of the given permission
  // type, enforcing the access-mode and forbidden-to-write rules. `is_move_or`
  // related special handling (e.g. Copy source is read, destination is write)
  // is done by the caller by passing the right permission type per path.
  ResolvedPathAccess ResolvePath(const std::string& virtual_path,
      Tool::PermissionType permission_type) const;

  // Returns true when `real_path` matches an entry of the forbidden-to-write
  // list.
  bool IsForbiddenToWrite(const std::string& real_path) const;

  const MountTable& mount_table() const { return context_.mount_table; }

  const PermissionContext& context() const { return context_; }

 private:
  PermissionContext context_;
};

}  // namespace jiaolong