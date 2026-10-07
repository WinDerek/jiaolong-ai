#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "agent/tool/tool_find.h"

namespace jiaolong {

namespace {

// Returns true if the directory is one that should be skipped entirely when
// walking the root path. These directories commonly contain generated,
// vendored or build outputs that would otherwise produce too many irrelevant
// results.
bool UtilIsIgnoredDirectory(const std::filesystem::path& directory_path) {
  static const std::unordered_set<std::string> kIgnoredDirectoryNames = {
    "node_modules", ".git", ".svn", ".hg",
    "build", "dist", "out", "target",
    "__pycache__", ".cache", ".venv", "venv",
    ".idea", ".vscode", ".gradle",
    "Pods", "bower_components", ".pytest_cache", ".mypy_cache", ".tox",
    ".jj", ".kotlin"
  };
  return kIgnoredDirectoryNames.count(
      directory_path.filename().string()) > 0;
}

// Returns true if the file content contains a NUL byte, which means it is
// a binary file. Binary files are skipped by FindStrings.
bool UtilIsBinaryFile(const std::filesystem::path& file_path) {
  std::ifstream file(file_path, std::ios::binary);
  if (!file.is_open()) {
    return true;
  }
  char buffer[4096];
  while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
    for (std::streamsize i = 0; i < file.gcount(); ++i) {
      if (buffer[i] == '\0') {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindFilesOutput>
ToolFindFiles::FindFiles(const ToolFindFindFilesInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  // Parse input
  const std::string& root_path = input.root_path_;
  const std::string& query = input.query_;

  // Validate input: the root path must be an existing directory and the
  // query must be non-empty. An empty query would list the whole directory
  // tree, which floods the context with irrelevant files; use the
  // ListDirectory tool instead to explore the folder structure.
  if (root_path.empty() || !std::filesystem::exists(root_path) ||
      !std::filesystem::is_directory(root_path) || query.empty()) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(
          duration_in_ms, "Invalid root path or empty query"),
      ToolFindFindFilesOutput({})
    };
  }

  // Recursively collect all files whose file name contains the query.
  // Ignored directories (e.g. node_modules) are not descended into.
  std::vector<std::string> present_files;
  std::error_code error_code;
  const std::filesystem::recursive_directory_iterator begin(
      root_path, std::filesystem::directory_options::skip_permission_denied,
      error_code);
  if (error_code) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseExecutionFailure>(
          duration_in_ms, std::string("Error walking root path: ") +
              error_code.message()),
      ToolFindFindFilesOutput({})
    };
  }
  const std::filesystem::recursive_directory_iterator end;
  for (std::filesystem::recursive_directory_iterator it = begin; it != end;
      it.increment(error_code)) {
    if (error_code) {
      const auto ending_time = std::chrono::steady_clock::now();
      const long long duration_in_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              ending_time - beginning_time).count();
      return {
        std::make_shared<ToolUseExecutionFailure>(
            duration_in_ms, std::string("Error walking root path: ") +
                error_code.message()),
        ToolFindFindFilesOutput({})
      };
    }
    const std::filesystem::path path = it->path();
    std::error_code entry_error_code;
    if (it->is_directory(entry_error_code)) {
      if (UtilIsIgnoredDirectory(path)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (entry_error_code || !it->is_regular_file(entry_error_code)) {
      continue;
    }
    if (path.filename().string().find(query) == std::string::npos) {
      continue;
    }
    present_files.push_back(path.string());
  }
  // Sort to get a deterministic order.
  std::sort(present_files.begin(), present_files.end());

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();

  return {
    std::make_shared<ToolUseSuccess>(duration_in_ms),
    ToolFindFindFilesOutput(present_files)
  };
}

std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindStringsOutput>
ToolFindStrings::FindStrings(const ToolFindFindStringsInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  // Parse input
  const std::string& root_path = input.root_path_;
  const std::string& query = input.query_;

  // Validate input: the root path must be an existing directory and the
  // query must be non-empty.
  if (root_path.empty() || !std::filesystem::exists(root_path) ||
      !std::filesystem::is_directory(root_path) || query.empty()) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(
          duration_in_ms,
          "Invalid root path or empty query"),
      ToolFindFindStringsOutput({})
    };
  }

  // Recursively collect all files that contain the query, together with
  // the 1-based line numbers where the query appears. Ignored directories
  // (e.g. node_modules) are not descended into. Binary files are skipped.
  std::vector<ToolFindFileWithLineNumbers> present_files;
  std::error_code error_code;
  const std::filesystem::recursive_directory_iterator begin(
      root_path, std::filesystem::directory_options::skip_permission_denied,
      error_code);
  if (error_code) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseExecutionFailure>(
          duration_in_ms, std::string("Error walking root path: ") +
              error_code.message()),
      ToolFindFindStringsOutput({})
    };
  }
  const std::filesystem::recursive_directory_iterator end;
  for (std::filesystem::recursive_directory_iterator it = begin; it != end;
      it.increment(error_code)) {
    if (error_code) {
      const auto ending_time = std::chrono::steady_clock::now();
      const long long duration_in_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              ending_time - beginning_time).count();
      return {
        std::make_shared<ToolUseExecutionFailure>(
            duration_in_ms, std::string("Error walking root path: ") +
                error_code.message()),
        ToolFindFindStringsOutput({})
      };
    }
    const std::filesystem::path path = it->path();
    std::error_code entry_error_code;
    if (it->is_directory(entry_error_code)) {
      if (UtilIsIgnoredDirectory(path)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (entry_error_code || !it->is_regular_file(entry_error_code) ||
        UtilIsBinaryFile(path)) {
      continue;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
      continue;
    }
    std::string current_line;
    int current_line_number = 0;
    std::vector<int> present_line_numbers;
    while (std::getline(file, current_line)) {
      current_line_number++;
      if (current_line.find(query) != std::string::npos) {
        present_line_numbers.push_back(current_line_number);
      }
    }
    if (!present_line_numbers.empty()) {
      present_files.push_back(
          ToolFindFileWithLineNumbers(path.string(), present_line_numbers));
    }
  }
  // Sort to get a deterministic order.
  std::sort(present_files.begin(), present_files.end(),
      [](const ToolFindFileWithLineNumbers& left,
         const ToolFindFileWithLineNumbers& right) {
        return left.file_path_ < right.file_path_;
      });

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();

  return {
    std::make_shared<ToolUseSuccess>(duration_in_ms),
    ToolFindFindStringsOutput(present_files)
  };
}

}  // namespace jiaolong
