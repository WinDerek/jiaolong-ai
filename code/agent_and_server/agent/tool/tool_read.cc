#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <memory>
#include <utility>

#include "agent/tool/tool_read.h"

namespace jiaolong {

namespace {

// The maximum number of lines a read tool may return. Larger outputs are
// truncated to avoid spending too many tokens.
constexpr int kReadOutputMaxLines = 1000;
constexpr char kReadOutputTruncatedIndicator[] =
    "... truncated due to lines limit (max 1000).";

std::string UtilReadWholeFile(const std::ifstream& file) {
  // Read the entire file buffer
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// Returns true if the content contains a NUL byte, which means the file is
// a binary file. Binary content cannot be read as text, so the read tools
// report it as such.
bool UtilContentIsBinary(const std::string& content) {
  return content.find('\0') != std::string::npos;
}

// Returns true if the file contains a NUL byte anywhere, which means it is
// a binary file. Used when only part of the file is read, so that a binary
// file is detected even when the NUL bytes fall outside the requested range.
// Restores the stream position before returning.
bool UtilFileIsBinary(std::ifstream& file) {
  const std::streampos original_position = file.tellg();
  char buffer[4096];
  bool is_binary = false;
  while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
    for (std::streamsize i = 0; i < file.gcount(); ++i) {
      if (buffer[i] == '\0') {
        is_binary = true;
        break;
      }
    }
    if (is_binary) {
      break;
    }
  }
  file.clear();
  file.seekg(original_position);
  return is_binary;
}

int UtilCountLines(std::ifstream& file) {
  std::string current_line;
  int current_line_idx = 0;

  // Loop through the file line by line
  while (std::getline(file, current_line)) {
      current_line_idx++;
  }

  return current_line_idx;
}

// Returns `content` unchanged if it has at most `max_lines` lines. Otherwise
// returns only the first `max_lines` lines followed by `indicator`, so the
// output stays within the line limit.
std::string UtilLimitOutputLines(const std::string& content,
    const int max_lines, const std::string& indicator) {
  // Count the number of lines in the content.
  int line_count = 0;
  for (std::string::size_type pos = 0; pos < content.size();) {
    const std::string::size_type newline_pos = content.find('\n', pos);
    if (newline_pos == std::string::npos) {
      ++line_count;
      break;
    }
    ++line_count;
    pos = newline_pos + 1;
  }

  // Nothing to truncate when the content is within the limit.
  if (line_count <= max_lines) {
    return content;
  }

  // Keep the first `max_lines` lines and append the indicator at the end.
  std::string truncated_content;
  std::istringstream input(content);
  std::string current_line;
  int kept_line_count = 0;
  while (kept_line_count < max_lines && std::getline(input, current_line)) {
    truncated_content += current_line;
    truncated_content += '\n';
    ++kept_line_count;
  }
  truncated_content += indicator;
  return truncated_content;
}

std::string UtilReadFileOfLineRange(std::ifstream& file,
    const int& beginning_line_idx, const int& ending_line_idx) {
  std::string current_line;
  std::string content;
  int current_line_idx = 0;

  // Loop through the file line by line
  while (std::getline(file, current_line)) {
      // Append lines that fall within the target range
      if (current_line_idx >= beginning_line_idx && current_line_idx <= ending_line_idx) {
        content += current_line + "\n";
      }

      // Stop reading early as soon as the end line is passed
      if (current_line_idx >= ending_line_idx) {
        break;
      }

      current_line_idx++;
  }

  return content;
}

}

std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadWholeFileOutput> ToolReadWholeFile::ReadWholeFile(
    const ToolReadReadWholeFileInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  // Parse input
  const std::string& file_path = input.file_path_;

  // Read file content
  std::ifstream file(file_path);
  if (!file.is_open()) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(duration_in_ms, "Invalid file path"),
      ToolReadReadWholeFileOutput("")
    };
  }
  std::string file_content = UtilReadWholeFile(file);
  if (UtilContentIsBinary(file_content)) {
    // Binary content cannot be read as text, so report it as such.
    file_content = "binary file";
  }
  // Limit the output so that reading a very large file does not cost too
  // many tokens.
  file_content = UtilLimitOutputLines(
      file_content, kReadOutputMaxLines, kReadOutputTruncatedIndicator);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();

  return {
    std::make_shared<ToolUseSuccess>(duration_in_ms),
    ToolReadReadWholeFileOutput(file_content)
  };
}

std::pair<std::shared_ptr<ToolUseStatus>, ToolReadCountLinesOutput> ToolCountLines::CountLines(
    const ToolReadCountLinesInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  // Parse input
  const std::string& file_path = input.file_path_;

  // Read file content
  std::ifstream file(file_path);
  if (!file.is_open()) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(duration_in_ms, "Invalid file path"),
      ToolReadCountLinesOutput(-1)
    };
  }
  const int num_lines = UtilCountLines(file);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();

  return {
    std::make_shared<ToolUseSuccess>(duration_in_ms),
    ToolReadCountLinesOutput(num_lines)
  };
}

std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadFileOfLineRangeOutput>
ToolReadFileOfLineRange::ReadFileOfLineRange(const ToolReadReadFileOfLineRangeInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  // Parse input
  const std::string& file_path = input.file_path_;
  const int& beginning_line_idx = input.beginning_line_number_ - 1;
  const int& ending_line_idx = input.ending_line_number_ - 1;

  // Read file content
  std::ifstream file(file_path);
  if (!file.is_open()) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseInvalidInput>(duration_in_ms, "Invalid file path"),
      ToolReadReadFileOfLineRangeOutput("")
    };
  }
  if (UtilFileIsBinary(file)) {
    // Binary content cannot be read as text, so report it as such.
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    return {
      std::make_shared<ToolUseSuccess>(duration_in_ms),
      ToolReadReadFileOfLineRangeOutput("binary file")
    };
  }
  std::string file_content = UtilReadFileOfLineRange(
      file, beginning_line_idx, ending_line_idx);
  // Limit the output so that reading a very large line range does not cost
  // too many tokens.
  file_content = UtilLimitOutputLines(
      file_content, kReadOutputMaxLines, kReadOutputTruncatedIndicator);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();

  return {
    std::make_shared<ToolUseSuccess>(duration_in_ms),
    ToolReadReadFileOfLineRangeOutput(file_content)
  };
}

}  // namespace jiaolong
