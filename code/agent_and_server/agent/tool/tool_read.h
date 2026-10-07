#pragma once

#include <memory>
#include <utility>

#include "agent/tool/tool.h"

namespace jiaolong {

// Input and output for ReadWholeFile
class ToolReadReadWholeFileInput : public ToolUseInput {
 public:
  const std::string file_path_;
  ToolReadReadWholeFileInput(const std::string& file_path) :
      ToolUseInput(ToolType::kRead), file_path_(file_path) {};
};
class ToolReadReadWholeFileOutput : public ToolUseOutput {
 public:
  const std::string file_content_;
  ToolReadReadWholeFileOutput(const std::string& file_content) :
      ToolUseOutput(ToolType::kRead), file_content_(file_content) {}
};

// Input and output for CountLines
class ToolReadCountLinesInput : public ToolUseInput {
 public:
  const std::string file_path_;
  ToolReadCountLinesInput(const std::string& file_path) :
      ToolUseInput(ToolType::kRead), file_path_(file_path) {};
};
class ToolReadCountLinesOutput : public ToolUseOutput {
 public:
  const int num_lines_;
  ToolReadCountLinesOutput(const int& num_lines) :
      ToolUseOutput(ToolType::kRead), num_lines_(num_lines) {}
};

// Input and output for ReadFileOfLineRange
class ToolReadReadFileOfLineRangeInput : public ToolUseInput {
 public:
  const std::string file_path_;
  const int beginning_line_number_;
  const int ending_line_number_;
  ToolReadReadFileOfLineRangeInput(const std::string& file_path,
      const int& beginning_line_number, const int& ending_line_number) :
      ToolUseInput(ToolType::kRead), file_path_(file_path),
      beginning_line_number_(beginning_line_number),
      ending_line_number_(ending_line_number) {};
};
class ToolReadReadFileOfLineRangeOutput : public ToolUseOutput {
 public:
  const std::string file_content_;
  ToolReadReadFileOfLineRangeOutput(const std::string& file_content) :
      ToolUseOutput(ToolType::kRead), file_content_(file_content) {}
};

// Reads a whole file as a single string.
class ToolReadWholeFile : public Tool {

 public:

  ToolReadWholeFile() : Tool(ToolType::kRead, PermissionType::kRead) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadWholeFileOutput> ReadWholeFile(
      const ToolReadReadWholeFileInput& input);

};

// Counts the number of lines in a file.
class ToolCountLines : public Tool {

 public:

  ToolCountLines() : Tool(ToolType::kRead, PermissionType::kRead) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadCountLinesOutput> CountLines(
      const ToolReadCountLinesInput& input);

};

// Reads a line range of a file.
class ToolReadFileOfLineRange : public Tool {

 public:

  ToolReadFileOfLineRange() : Tool(ToolType::kRead, PermissionType::kRead) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadFileOfLineRangeOutput>
  ReadFileOfLineRange(const ToolReadReadFileOfLineRangeInput& input);

};

}  // namespace jiaolong
