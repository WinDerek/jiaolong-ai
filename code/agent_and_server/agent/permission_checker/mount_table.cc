#include "agent/permission_checker/mount_table.h"

#include <algorithm>
#include <string>
#include <system_error>
#include <utility>

#include "agent/permission_checker/path_utils.h"

namespace jiaolong {

namespace {

constexpr char kReservedAlias[] = "workspace";

// Removes any trailing '/' from `path` except when the path is exactly "/".
std::string StripTrailingSlashes(std::string path) {
  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }
  return path;
}

// Sorts mounts by decreasing virtual-root length so that the deepest (longest)
// prefix wins during resolution. The sort is stable so mounts of equal length
// keep their insertion order.
std::vector<Mount> SortedByVirtualRootLength(const std::vector<Mount>& mounts) {
  std::vector<Mount> sorted = mounts;
  std::stable_sort(sorted.begin(), sorted.end(),
      [](const Mount& a, const Mount& b) {
        return a.virtual_root.size() > b.virtual_root.size();
      });
  return sorted;
}

// Sorts mounts by decreasing real-root length so that the deepest mount wins
// when mapping a real path back to a virtual path.
std::vector<Mount> SortedByRealRootLength(const std::vector<Mount>& mounts) {
  std::vector<Mount> sorted = mounts;
  std::stable_sort(sorted.begin(), sorted.end(),
      [](const Mount& a, const Mount& b) {
        return a.real_root.string().size() > b.real_root.string().size();
      });
  return sorted;
}

}  // namespace

bool IsValidReadonlyDirectoryAlias(const std::string& alias) {
  if (alias.empty() || alias == kReservedAlias) {
    return false;
  }
  for (const char c : alias) {
    const bool is_letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    const bool is_digit = c >= '0' && c <= '9';
    if (!is_letter && !is_digit && c != '_' && c != '-') {
      return false;
    }
  }
  return true;
}

MountTable MountTable::SingleWorkspaceMount(
    const std::string& working_directory) {
  MountTable table;
  Mount workspace;
  workspace.virtual_root = "/workspace";
  workspace.real_root =
      std::filesystem::path(StripTrailingSlashes(working_directory));
  workspace.access = MountAccess::kReadWrite;
  table.mounts_.push_back(std::move(workspace));
  return table;
}

MountTable MountTable::FromTask(const Task& task, const Project& project) {
  MountTable table;

  Mount workspace;
  workspace.virtual_root = "/workspace";
  workspace.real_root =
      std::filesystem::path(StripTrailingSlashes(task.working_directory_));
  workspace.access = MountAccess::kReadWrite;
  table.mounts_.push_back(std::move(workspace));

  // Only the readonly directories the task selected become mounts. An
  // unselected project directory behaves exactly as if it did not exist.
  for (const std::string& readonly_directory_id : task.readonly_directories_) {
    const ReadonlyDirectory* selected = nullptr;
    for (const ReadonlyDirectory& directory : project.readonly_directories_) {
      if (!directory.id_.empty() &&
          directory.id_ == readonly_directory_id) {
        selected = &directory;
        break;
      }
    }
    if (selected == nullptr) {
      // The catalog entry was deleted (or the id is unknown): ignore it.
      continue;
    }
    if (!IsValidReadonlyDirectoryAlias(selected->alias_)) {
      continue;
    }
    if (selected->real_path_.empty()) {
      continue;
    }
    // A readonly directory must map to an absolute host directory.
    if (!std::filesystem::path(selected->real_path_).is_absolute()) {
      continue;
    }
    // An alias must be unique across mounts; a duplicate is discarded rather
    // than silently shadowing another mount.
    const std::string virtual_root = "/readonly/" + selected->alias_;
    const bool duplicate = std::any_of(table.mounts_.begin(),
        table.mounts_.end(), [&](const Mount& mount) {
          return mount.virtual_root == virtual_root;
        });
    if (duplicate) {
      continue;
    }

    Mount mount;
    mount.virtual_root = virtual_root;
    mount.real_root = std::filesystem::path(selected->real_path_);
    mount.access = MountAccess::kReadOnly;
    mount.description = selected->description_;
    mount.readonly_directory_id = selected->id_;
    mount.alias = selected->alias_;
    table.mounts_.push_back(std::move(mount));
  }

  return table;
}

int MountTable::readonly_mount_count() const {
  int count = 0;
  for (const Mount& mount : mounts_) {
    if (mount.access == MountAccess::kReadOnly) {
      ++count;
    }
  }
  return count;
}

std::optional<ResolvedPath> MountTable::ToRealPath(
    const std::string& virtual_path) const {
  // Virtual paths must be absolute and use '/' separators.
  if (virtual_path.empty() || virtual_path[0] != '/') {
    return std::nullopt;
  }

  const std::vector<Mount> sorted = SortedByVirtualRootLength(mounts_);
  for (const Mount& mount : sorted) {
    const bool is_exact_root = virtual_path == mount.virtual_root;
    const bool is_child =
        virtual_path.size() > mount.virtual_root.size() &&
        virtual_path.compare(0, mount.virtual_root.size(),
            mount.virtual_root) == 0 &&
        virtual_path[mount.virtual_root.size()] == '/';
    if (!is_exact_root && !is_child) {
      continue;
    }

    std::string suffix = virtual_path.substr(mount.virtual_root.size());
    // Drop the leading '/' so appending the suffix yields a real child path;
    // otherwise std::filesystem would treat the suffix as an absolute path and
    // discard the mount root. An empty suffix (the mount root itself) maps to
    // the mount root unchanged.
    if (!suffix.empty() && suffix[0] == '/') {
      suffix.erase(0, 1);
    }
    std::filesystem::path real_path = mount.real_root;
    if (!suffix.empty()) {
      real_path /= suffix;
    }
    real_path = real_path.lexically_normal();

    std::error_code error_code;
    const std::filesystem::path canonical_real =
        std::filesystem::weakly_canonical(real_path, error_code);
    const std::filesystem::path canonical_root =
        std::filesystem::weakly_canonical(mount.real_root, error_code);

    // The mapped path must stay same-or-sub directory of the (canonical) mount
    // root; this blocks `..` traversal and symlink escapes.
    if (!IsSameOrSubDirectory(canonical_real, canonical_root)) {
      return std::nullopt;
    }

    ResolvedPath resolved;
    resolved.real_path = canonical_real;
    resolved.virtual_root = mount.virtual_root;
    resolved.access = mount.access;
    return resolved;
  }
  return std::nullopt;
}

std::optional<std::string> MountTable::ToVirtualPath(
    const std::filesystem::path& real_path) const {
  const std::vector<Mount> sorted = SortedByRealRootLength(mounts_);
  for (const Mount& mount : sorted) {
    std::error_code error_code;
    const std::filesystem::path canonical_real =
        std::filesystem::weakly_canonical(real_path, error_code);
    const std::filesystem::path canonical_root =
        std::filesystem::weakly_canonical(mount.real_root, error_code);
    if (!IsSameOrSubDirectory(canonical_real, canonical_root)) {
      continue;
    }
    std::filesystem::path relative_path =
        canonical_real.lexically_normal().lexically_relative(
            canonical_root.lexically_normal());
    std::string suffix = relative_path.generic_string();
    if (suffix == ".") {
      suffix = "";
    }
    if (suffix.empty()) {
      return mount.virtual_root;
    }
    return mount.virtual_root + "/" + suffix;
  }
  return std::nullopt;
}

}  // namespace jiaolong