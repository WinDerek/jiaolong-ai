#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "agent/project.h"
#include "agent/task.h"

namespace jiaolong {

// Access mode of a mount. Read-only mounts may be read but never written.
enum class MountAccess {
  kReadWrite,
  kReadOnly,
};

// A mapping from a virtual root (as seen by the LLM) to a real host directory
// plus an access mode.
struct Mount {
  // "/workspace" or "/readonly/<alias>".
  std::string virtual_root;
  // Absolute host directory.
  std::filesystem::path real_root;
  MountAccess access = MountAccess::kReadWrite;
  // Human-readable description shown to the LLM in the system prompt. Empty
  // for the primary mount.
  std::string description;
  // The project catalog id this mount was built from. Empty for the primary
  // mount.
  std::string readonly_directory_id;
  // The alias of the readonly directory. Empty for the primary mount.
  std::string alias;
};

// The result of resolving a virtual path against a mount table. Carries the
// mapped real path, the matched mount and its access mode so the permission
// checker does not have to re-derive them.
struct ResolvedPath {
  std::filesystem::path real_path;
  std::string virtual_root;
  MountAccess access = MountAccess::kReadWrite;
};

// The ordered list of mounts for the task being worked on: the primary RW mount
// plus one RO mount per readonly directory the task selected.
class MountTable {
 public:
  // Builds ["/workspace" -> task.working_directory (RW)] plus one RO mount per
  // readonly directory of `project` that `task` has selected. The selection is
  // a subset of the project's readonly-directory catalog and is empty by
  // default, so a task with no selection mounts only "/workspace".
  static MountTable FromTask(const Task& task, const Project& project);

  // Builds a mount table whose only mount is the implicit RW "/workspace"
  // mount pointing at `working_directory`. Used for backward compatibility
  // when no project catalog is available.
  static MountTable SingleWorkspaceMount(const std::string& working_directory);

  bool empty() const { return mounts_.empty(); }

  // Number of read-only mounts in the table.
  int readonly_mount_count() const;

  // Ordered mounts (primary first, then selected readonly directories).
  const std::vector<Mount>& mounts() const { return mounts_; }

  // Longest virtual-root prefix match. Returns the real path and access mode,
  // or std::nullopt when the path is not under any mount or escapes its mount
  // root (e.g. through `..` or a symlink).
  std::optional<ResolvedPath> ToRealPath(const std::string& virtual_path) const;

  // Reverse direction (longest real-root prefix match) for tool outputs.
  // Returns std::nullopt when the real path is not under any mount.
  std::optional<std::string> ToVirtualPath(
      const std::filesystem::path& real_path) const;

 private:
  std::vector<Mount> mounts_;
};

// Returns true when `alias` is a valid readonly-directory alias: non-empty and
// matching ^[A-Za-z0-9_-]+$, and not the reserved word "workspace".
bool IsValidReadonlyDirectoryAlias(const std::string& alias);

}  // namespace jiaolong