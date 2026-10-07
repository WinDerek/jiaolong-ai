#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "agent/tool/tool_copy.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {

std::pair<std::shared_ptr<ToolUseStatus>, ToolCopyOutput>
ToolCopy::Copy(const ToolCopyInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  const std::string& source_path = input.source_path_;
  const std::string& destination_path = input.destination_path_;

  // Validate input: the source path must be non-empty and refer to an
  // existing file or directory, and the destination path must be non-empty.
  // The non-throwing overload is used so that filesystem errors (e.g.
  // permission denied) are reported through the error code instead of
  // throwing an exception.
  std::error_code source_exists_error_code;
  const bool source_exists = std::filesystem::exists(
      source_path, source_exists_error_code);
  if (source_path.empty() || destination_path.empty() ||
      source_exists_error_code || !source_exists) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(
          duration_in_ms,
          "Invalid path: source path is empty, destination path is empty, "
          "or source does not exist"),
      ToolCopyOutput()
    };
  }

  // Resolve the effective destination. When the destination refers to an
  // existing directory, the source is copied into that directory under its
  // file name (`cp -r source dest` semantics).
  std::filesystem::path real_destination_path(destination_path);
  // The non-throwing overload is used so that a filesystem error while
  // inspecting the destination is reported through the error code instead of
  // throwing. On an error the destination is treated as a non-directory and
  // any failure is caught by the copy operation below.
  std::error_code destination_is_directory_error_code;
  const bool destination_is_directory = std::filesystem::is_directory(
      real_destination_path, destination_is_directory_error_code);
  if (!destination_is_directory_error_code && destination_is_directory) {
    real_destination_path = real_destination_path /
        std::filesystem::path(source_path).filename();
  }

  // Copying a path onto itself is a no-op; treat it as success.
  std::error_code equivalent_error_code;
  const bool is_same_path = std::filesystem::equivalent(
      source_path, real_destination_path.string(), equivalent_error_code);
  if (!equivalent_error_code && is_same_path) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseSuccess>(duration_in_ms),
      ToolCopyOutput()
    };
  }

  // Prevent copying a directory into one of its own subdirectories, which
  // would otherwise recurse indefinitely. The non-throwing overload is used
  // so that a filesystem error here is reported through the error code
  // instead of throwing an exception.
  std::error_code source_is_directory_error_code;
  const bool source_is_directory = std::filesystem::is_directory(
      source_path, source_is_directory_error_code);
  if (!source_is_directory_error_code && source_is_directory) {
    std::error_code source_absolute_error_code;
    const std::filesystem::path absolute_source =
        std::filesystem::absolute(source_path, source_absolute_error_code);
    std::error_code destination_absolute_error_code;
    const std::filesystem::path absolute_destination =
        std::filesystem::absolute(real_destination_path,
            destination_absolute_error_code);
    if (!source_absolute_error_code && !destination_absolute_error_code &&
        absolute_destination.string().rfind(
        absolute_source.string() +
        std::filesystem::path::preferred_separator, 0) == 0) {
      const auto ending_time = std::chrono::steady_clock::now();
      const long long duration_in_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              ending_time - beginning_time).count();
      return {
        std::make_shared<ToolUseInvalidInput>(
            duration_in_ms,
            "Invalid destination: cannot copy a directory into its own "
            "subdirectory"),
        ToolCopyOutput()
      };
    }
  }

  // Copy the file or directory. `recursive` copies directory trees and
  // `overwrite_existing` makes copying onto an existing path succeed.
  std::error_code error_code;
  std::filesystem::copy(source_path, real_destination_path,
      std::filesystem::copy_options::recursive |
      std::filesystem::copy_options::overwrite_existing,
      error_code);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();

  if (error_code) {
    return {
      std::make_shared<ToolUseExecutionFailure>(
          duration_in_ms, "Failed to copy path: " + error_code.message()),
      ToolCopyOutput()
    };
  }

  return {
    std::make_shared<ToolUseSuccess>(duration_in_ms),
    ToolCopyOutput()
  };
}

}  // namespace jiaolong