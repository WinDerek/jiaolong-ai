#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "agent/tool/tool_list_directory.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {

std::pair<std::shared_ptr<ToolUseStatus>, ToolListDirectoryOutput>
ToolListDirectory::ListDirectory(const ToolListDirectoryInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  const std::string& directory_path = input.directory_path_;

  // Validate input: the directory path must be non-empty and refer to an
  // existing directory.
  if (directory_path.empty() || !std::filesystem::exists(directory_path) ||
      !std::filesystem::is_directory(directory_path)) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(
          duration_in_ms, "Invalid directory path"),
      ToolListDirectoryOutput({})
    };
  }

  // List only the immediate children (files and directories) of the current
  // directory, so that the agent can explore the folder structure
  // incrementally without flooding the context with irrelevant files.
  std::vector<ToolListDirectoryEntry> entries;
  std::error_code error_code;
  const std::filesystem::directory_iterator begin(
      directory_path, std::filesystem::directory_options::skip_permission_denied,
      error_code);
  if (error_code) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseExecutionFailure>(
          duration_in_ms, std::string("Error listing directory: ") +
              error_code.message()),
      ToolListDirectoryOutput({})
    };
  }
  const std::filesystem::directory_iterator end;
  for (std::filesystem::directory_iterator it = begin; it != end;
      it.increment(error_code)) {
    if (error_code) {
      const auto ending_time = std::chrono::steady_clock::now();
      const long long duration_in_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              ending_time - beginning_time).count();
      return {
        std::make_shared<ToolUseExecutionFailure>(
            duration_in_ms, std::string("Error listing directory: ") +
                error_code.message()),
        ToolListDirectoryOutput({})
      };
    }
    const std::filesystem::path path = it->path();
    std::error_code entry_error_code;
    entries.push_back(ToolListDirectoryEntry(
        path.filename().string(), it->is_directory(entry_error_code)));
  }
  // Sort by name to get a deterministic order.
  std::sort(entries.begin(), entries.end(),
      [](const ToolListDirectoryEntry& left,
         const ToolListDirectoryEntry& right) {
        return left.name_ < right.name_;
      });

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();

  return {
    std::make_shared<ToolUseSuccess>(duration_in_ms),
    ToolListDirectoryOutput(entries)
  };
}

}  // namespace jiaolong
