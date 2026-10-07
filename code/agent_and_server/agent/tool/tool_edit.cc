#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "agent/tool/tool_edit.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {

namespace {

/**
 * Searches for all instances of old_str in the specified file and replaces them with new_str.
 * 
 * @param file_path The path to the file to modify.
 * @param old_str   The substring to search for.
 * @param new_str   The string to replace old_str with.
 * @return          true if successful, false if file operations fail or old_str is not found.
 */
bool UtilSearchAndReplace(const std::string& file_path,
    const std::string& old_str, const std::string& new_str) {
  // Prevent infinite loops and unnecessary processing if search string is empty
  if (old_str.empty()) {
    std::cerr << "Error: Search string cannot be empty.\n";
    return false; 
  }

  // 1. Read the entire file into a memory buffer
  std::ifstream in_file(file_path);
  if (!in_file.is_open()) {
    std::cerr << "Error: Could not open file for reading: " << file_path << "\n";
    return false;
  }

  std::stringstream buffer;
  buffer << in_file.rdbuf();
  std::string content = buffer.str();
  in_file.close();

  // 2. Perform the in-memory search and replace
  size_t pos = 0;
  bool made_changes = false;
  
  // Loop through the string, finding occurrences of old_str
  while ((pos = content.find(old_str, pos)) != std::string::npos) {
    content.replace(pos, old_str.length(), new_str);
    
    // Move pos forward by the length of the new string. 
    // This is crucial to prevent infinite loops if new_str contains old_str!
    pos += new_str.length();
    made_changes = true;
  }

  // If we didn't find anything to replace, we can exit early and save disk I/O
  if (!made_changes) {
    return false;
  }

  // 3. Write the modified content back to the file
  std::ofstream out_file(file_path, std::ios::trunc); // trunc ensures the file is overwritten
  if (!out_file.is_open()) {
    std::cerr << "Error: Could not open file for writing: " << file_path << "\n";
    return false;
  }

  out_file << content;
  out_file.close();

  return true;
}

}

bool UtilCreateNewFile(const std::string& file_path,
    const std::string& initial_content) {
  const std::filesystem::path path(file_path);
  std::error_code error_code;

  // Early return if the file already exists.
  if (std::filesystem::exists(path, error_code)) {
    std::cout << "File already exists: " << file_path << std::endl;
    return false;
  }
  if (error_code) {
    std::cerr << "Error: Failed to access path: " << file_path << ": "
              << error_code.message() << "\n";
    return false;
  }

  // Create parent directories if not exist.
  const std::filesystem::path parent_dir = path.parent_path();
  if (!parent_dir.empty() && !std::filesystem::exists(parent_dir, error_code)) {
    error_code.clear();
    std::filesystem::create_directories(parent_dir, error_code);
    if (error_code) {
      std::cerr << "Error: Failed to create parent directories: "
                << parent_dir.string() << ": " << error_code.message() << "\n";
      return false;
    }
  }

  // Create a new empty file.
  std::ofstream file(file_path, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    std::cerr << "Error: Could not create file: " << file_path << "\n";
    return false;
  }
  file << initial_content;
  file.close();
  return true;
}

bool UtilCreateNewDirectory(const std::string& directory_path) {
  const std::filesystem::path path(directory_path);
  std::error_code error_code;

  // Early return if the directory already exists.
  if (std::filesystem::exists(path, error_code)) {
    std::cout << "Path already exists: " << directory_path << std::endl;
    return false;
  }
  if (error_code) {
    std::cerr << "Error: Failed to access path: " << directory_path << ": "
              << error_code.message() << "\n";
    return false;
  }

  // Create directory along with parent directories if not exist.
  std::filesystem::create_directories(path, error_code);
  if (error_code) {
    std::cerr << "Error: Failed to create directory: " << directory_path
              << ": " << error_code.message() << "\n";
    return false;
  }

  return true;
}

/**
 * Writes the given content to the specified file, overwriting any existing
 * content. Unlike SearchAndReplace, this works even when the file is empty or
 * contains only a newline character.
 *
 * @param file_path The path to the file to write.
 * @param content   The content to write to the file.
 * @return          true if successful, false if file operations fail.
 */
bool UtilWriteContentToFile(const std::string& file_path,
    const std::string& content) {
  const std::filesystem::path path(file_path);
  std::error_code error_code;

  // Return an error if the target path is an existing directory; a
  // directory cannot be overwritten with file content.
  if (std::filesystem::exists(path, error_code) &&
      std::filesystem::is_directory(path, error_code)) {
    std::cerr << "Error: Cannot write to path because it is a directory: "
        << file_path << std::endl;
    return false;
  }
  if (error_code) {
    std::cerr << "Error: Failed to access path: " << file_path << ": "
              << error_code.message() << "\n";
    return false;
  }

  // Create parent directories if not exist.
  const std::filesystem::path parent_dir = path.parent_path();
  if (!parent_dir.empty() && !std::filesystem::exists(parent_dir, error_code)) {
    error_code.clear();
    std::filesystem::create_directories(parent_dir, error_code);
    if (error_code) {
      std::cerr << "Error: Failed to create parent directories: "
                << parent_dir.string() << ": " << error_code.message() << "\n";
      return false;
    }
  }

  // Write the content to the file, truncating any existing content.
  std::ofstream file(file_path, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file for writing: " << file_path << "\n";
    return false;
  }
  file << content;
  file.close();
  return true;
}

std::pair<std::shared_ptr<ToolUseStatus>, ToolEditSearchAndReplaceOutput>
ToolSearchAndReplace::SearchAndReplace(const ToolEditSearchAndReplaceInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  bool success = UtilSearchAndReplace(
      input.file_path_, input.old_str_, input.new_str_);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();
  std::shared_ptr<ToolUseStatus> tool_use_status = nullptr;
  if (success) {
    tool_use_status = std::make_shared<ToolUseSuccess>(duration_in_ms);
  } else {
    tool_use_status = std::make_shared<ToolUseExecutionFailure>(
        duration_in_ms, "Execution failure");
  }
  return { tool_use_status, ToolEditSearchAndReplaceOutput() };
}

std::pair<std::shared_ptr<ToolUseStatus>, ToolEditCreateNewFileOutput>
ToolCreateNewFile::CreateNewFile(const ToolEditCreateNewFileInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  // Ensure file path parameter is non-empty.
  if (input.file_path_.empty()) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    auto status = std::make_shared<ToolUseInvalidInput>(
        duration_in_ms, "file path must be non-empty");
    return { status, ToolEditCreateNewFileOutput() };
  }

  bool success = UtilCreateNewFile(input.file_path_, input.initial_content_);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();
  std::shared_ptr<ToolUseStatus> tool_use_status = nullptr;
  if (success) {
    tool_use_status = std::make_shared<ToolUseSuccess>(duration_in_ms);
  } else {
    tool_use_status = std::make_shared<ToolUseExecutionFailure>(
        duration_in_ms, "Execution failure");
  }
  return { tool_use_status, ToolEditCreateNewFileOutput() };
}

std::pair<std::shared_ptr<ToolUseStatus>, ToolEditCreateNewDirectoryOutput>
ToolCreateNewDirectory::CreateNewDirectory(const ToolEditCreateNewDirectoryInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  // Ensure file path parameter is non-empty.
  if (input.directory_path_.empty()) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    auto status = std::make_shared<ToolUseInvalidInput>(
        duration_in_ms, "file path must be non-empty");
    return { status, ToolEditCreateNewDirectoryOutput() };
  }

  bool success = UtilCreateNewDirectory(input.directory_path_);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();
  std::shared_ptr<ToolUseStatus> tool_use_status = nullptr;
  if (success) {
    tool_use_status = std::make_shared<ToolUseSuccess>(duration_in_ms);
  } else {
    tool_use_status = std::make_shared<ToolUseExecutionFailure>(
        duration_in_ms, "Execution failure");
  }
  return { tool_use_status, ToolEditCreateNewDirectoryOutput() };
}

std::pair<std::shared_ptr<ToolUseStatus>, ToolEditWriteContentToFileOutput>
ToolWriteContentToFile::WriteContentToFile(const ToolEditWriteContentToFileInput& input) {
  const auto beginning_time = std::chrono::steady_clock::now();

  // Ensure file path parameter is non-empty.
  if (input.file_path_.empty()) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    auto status = std::make_shared<ToolUseInvalidInput>(
        duration_in_ms, "file path must be non-empty");
    return { status, ToolEditWriteContentToFileOutput() };
  }

  // Ensure the file path is not an existing directory; content can only be
  // written to a file, not to a directory.
  if (std::filesystem::exists(input.file_path_) &&
      std::filesystem::is_directory(input.file_path_)) {
    const auto ending_time = std::chrono::steady_clock::now();
    const long long duration_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ending_time - beginning_time).count();
    auto status = std::make_shared<ToolUseInvalidInput>(
        duration_in_ms, "file path is a directory: " + input.file_path_);
    return { status, ToolEditWriteContentToFileOutput() };
  }

  bool success = UtilWriteContentToFile(input.file_path_, input.content_);

  const auto ending_time = std::chrono::steady_clock::now();
  const long long duration_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          ending_time - beginning_time).count();
  std::shared_ptr<ToolUseStatus> tool_use_status = nullptr;
  if (success) {
    tool_use_status = std::make_shared<ToolUseSuccess>(duration_in_ms);
  } else {
    tool_use_status = std::make_shared<ToolUseExecutionFailure>(
        duration_in_ms, "Execution failure");
  }
  return { tool_use_status, ToolEditWriteContentToFileOutput() };
}

}  // namespace jiaolong
