#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "agent/tool/tool_delete.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {

std::pair<std::shared_ptr<ToolUseStatus>, ToolDeleteOutput>
ToolDelete::Delete(const ToolDeleteInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  const std::string& path = input.path_;

  // Validate input: the path must be non-empty and refer to an existing
  // file or directory.
  if (path.empty() || !std::filesystem::exists(path)) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(
          duration_in_ms, "Invalid path: path is empty or does not exist"),
      ToolDeleteOutput()
    };
  }

  // Delete the single file or directory. remove_all works for both a single
  // file and a directory tree (deleting a directory recursively).
  std::error_code error_code;
  const std::uintmax_t removed_count =
      std::filesystem::remove_all(path, error_code);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();

  if (error_code || removed_count == 0) {
    const std::string error_message = error_code
        ? error_code.message() : "nothing was removed";
    return {
      std::make_shared<ToolUseExecutionFailure>(
          duration_in_ms, "Failed to delete path: " + error_message),
      ToolDeleteOutput()
    };
  }

  return {
    std::make_shared<ToolUseSuccess>(duration_in_ms),
    ToolDeleteOutput()
  };
}

}  // namespace jiaolong
