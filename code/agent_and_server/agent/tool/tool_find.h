#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "agent/tool/tool.h"

namespace jiaolong {

// A file that contains the searched string, together with the 1-based line
// numbers where the string appears. The members are non-const so that the
// entries can be sorted by std::sort.
class ToolFindFileWithLineNumbers {
 public:
  std::string file_path_;
  std::vector<int> present_line_numbers_;
  ToolFindFileWithLineNumbers(const std::string& file_path,
      const std::vector<int>& present_line_numbers) :
      file_path_(file_path), present_line_numbers_(present_line_numbers) {}
  bool operator==(const ToolFindFileWithLineNumbers& other) const {
    return file_path_ == other.file_path_ &&
        present_line_numbers_ == other.present_line_numbers_;
  }
};

// Input and output for FindFiles
class ToolFindFindFilesInput : public ToolUseInput {
 public:
  const std::string root_path_;
  const std::string query_;
  ToolFindFindFilesInput(const std::string& root_path,
      const std::string& query) :
      ToolUseInput(ToolType::kFind), root_path_(root_path), query_(query) {};
};
class ToolFindFindFilesOutput : public ToolUseOutput {
 public:
  const std::vector<std::string> present_files_;
  ToolFindFindFilesOutput(const std::vector<std::string>& present_files) :
      ToolUseOutput(ToolType::kFind), present_files_(present_files) {}
};

// Input and output for FindStrings
class ToolFindFindStringsInput : public ToolUseInput {
 public:
  const std::string root_path_;
  const std::string query_;
  ToolFindFindStringsInput(const std::string& root_path,
      const std::string& query) :
      ToolUseInput(ToolType::kFind), root_path_(root_path), query_(query) {};
};
class ToolFindFindStringsOutput : public ToolUseOutput {
 public:
  const std::vector<ToolFindFileWithLineNumbers> present_files_;
  ToolFindFindStringsOutput(
      const std::vector<ToolFindFileWithLineNumbers>& present_files) :
      ToolUseOutput(ToolType::kFind), present_files_(present_files) {}
};

// Recursively finds files whose file name contains the query.
class ToolFindFiles : public Tool {

 public:

  ToolFindFiles() : Tool(ToolType::kFind, PermissionType::kRead) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindFilesOutput> FindFiles(
      const ToolFindFindFilesInput& input);

};

// Recursively finds files that contain the query string.
class ToolFindStrings : public Tool {

 public:

  ToolFindStrings() : Tool(ToolType::kFind, PermissionType::kRead) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindStringsOutput>
  FindStrings(const ToolFindFindStringsInput& input);

};

}  // namespace jiaolong
