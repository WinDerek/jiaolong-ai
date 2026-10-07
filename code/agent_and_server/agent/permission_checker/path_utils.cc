#include <filesystem>
#include <vector>
#include <iostream>

#include "agent/permission_checker/path_utils.h"

namespace jiaolong {

bool IsSameOrSubDirectory(const std::filesystem::path& child,
    const std::filesystem::path& parent) {
  // Normalize both paths lexically (e.g., converts "a/b/../c" to "a/c")
  // This is a pure string operation and does NOT hit the disk.
  auto normal_child = child.lexically_normal();
  auto normal_parent = parent.lexically_normal();

  // Find the purely lexical relative path
  auto relative_path = normal_child.lexically_relative(normal_parent);

  // If roots don't match (e.g., "C:\foo" vs "D:\bar"), returns empty.
  // If child escapes the parent, it starts with "..".
  // Otherwise, it's the same directory or a child.
  return !relative_path.empty() && *relative_path.begin() != "..";
}

bool IsPathAllowed(const std::filesystem::path& path,
    const std::vector<std::filesystem::path>& allowed_paths) {
  for (const std::filesystem::path& allowed_path : allowed_paths) {
    if (IsSameOrSubDirectory(path, allowed_path)) {
      std::cout << "IsSameOrSubDirectory true, path: \"" << path.string() << "\", allowed_path: \"" << allowed_path.string() << "\"" << std::endl;
      return true;
    }
  }
  return false;
}

std::filesystem::path ToAbsolutePath(const std::filesystem::path& path,
    const std::filesystem::path& working_directory) {
  if (path.is_absolute()) {
    return path;
  }
  return working_directory / path;
}

}  // namespace jiaolong
