#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "agent/tool/tool_move.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {

std::pair<std::shared_ptr<ToolUseStatus>, ToolMoveOutput>
ToolMove::Move(const ToolMoveInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  const std::string& source_path = input.source_path_;
  const std::string& destination_path = input.destination_path_;

  // Validate input: the source path must be non-empty and refer to an
  // existing file or directory, and the destination path must be non-empty.
  if (source_path.empty() || destination_path.empty() ||
      !std::filesystem::exists(source_path)) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(
          duration_in_ms,
          "Invalid path: source path is empty, destination path is empty, "
          "or source does not exist"),
      ToolMoveOutput()
    };
  }

  // Moving a path onto itself is a no-op; treat it as success.
  std::error_code equivalent_error_code;
  const bool is_same_path = std::filesystem::equivalent(
      source_path, destination_path, equivalent_error_code);
  if (!equivalent_error_code && is_same_path) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseSuccess>(duration_in_ms),
      ToolMoveOutput()
    };
  }

  // Rename works for both a single file and a directory.
  std::error_code error_code;
  std::filesystem::rename(source_path, destination_path, error_code);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();

  if (error_code) {
    return {
      std::make_shared<ToolUseExecutionFailure>(
          duration_in_ms, "Failed to move path: " + error_code.message()),
      ToolMoveOutput()
    };
  }

  return {
    std::make_shared<ToolUseSuccess>(duration_in_ms),
    ToolMoveOutput()
  };
}

}  // namespace jiaolong
