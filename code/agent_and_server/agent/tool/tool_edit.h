#pragma once

#include <memory>
#include <utility>

#include "agent/tool/tool.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {

class ToolEditSearchAndReplaceInput : public ToolUseInput {
 public:
  const std::string file_path_;
  const std::string old_str_;
  const std::string new_str_;
  ToolEditSearchAndReplaceInput(const std::string& file_path,
      const std::string& old_str, const std::string& new_str) :
      ToolUseInput(ToolType::kEdit), file_path_(file_path),
      old_str_(old_str), new_str_(new_str) {};
};
class ToolEditSearchAndReplaceOutput : public ToolUseOutput {
 public:
  ToolEditSearchAndReplaceOutput() : ToolUseOutput(ToolType::kEdit) {}
};

class ToolEditCreateNewFileInput : public ToolUseInput {
 public:
  const std::string file_path_;
  const std::string initial_content_;
  ToolEditCreateNewFileInput(const std::string& file_path,
      const std::string& initial_content) : ToolUseInput(ToolType::kEdit),
      file_path_(file_path), initial_content_(initial_content) {};
};
class ToolEditCreateNewFileOutput : public ToolUseOutput {
 public:
  ToolEditCreateNewFileOutput() : ToolUseOutput(ToolType::kEdit) {}
};

class ToolEditCreateNewDirectoryInput : public ToolUseInput {
 public:
  const std::string directory_path_;
  ToolEditCreateNewDirectoryInput(const std::string& directory_path) :
      ToolUseInput(ToolType::kEdit), directory_path_(directory_path) {};
};
class ToolEditCreateNewDirectoryOutput : public ToolUseOutput {
 public:
  ToolEditCreateNewDirectoryOutput() : ToolUseOutput(ToolType::kEdit) {}
};

class ToolEditWriteContentToFileInput : public ToolUseInput {
 public:
  const std::string file_path_;
  const std::string content_;
  ToolEditWriteContentToFileInput(const std::string& file_path,
      const std::string& content) : ToolUseInput(ToolType::kEdit),
      file_path_(file_path), content_(content) {};
};
class ToolEditWriteContentToFileOutput : public ToolUseOutput {
 public:
  ToolEditWriteContentToFileOutput() : ToolUseOutput(ToolType::kEdit) {}
};

// Searches for all instances of `old_str` in a file and replaces them with
// `new_str`.
class ToolSearchAndReplace : public Tool {

 public:

  ToolSearchAndReplace() : Tool(ToolType::kEdit, PermissionType::kWrite) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolEditSearchAndReplaceOutput>
      SearchAndReplace(const ToolEditSearchAndReplaceInput& input);

};

// Creates a new file, optionally with initial content.
class ToolCreateNewFile : public Tool {

 public:

  ToolCreateNewFile() : Tool(ToolType::kEdit, PermissionType::kWrite) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolEditCreateNewFileOutput>
  CreateNewFile(const ToolEditCreateNewFileInput& input);

};

// Creates a new directory.
class ToolCreateNewDirectory : public Tool {

 public:

  ToolCreateNewDirectory() : Tool(ToolType::kEdit, PermissionType::kWrite) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolEditCreateNewDirectoryOutput>
  CreateNewDirectory(const ToolEditCreateNewDirectoryInput& input);

};

// Writes content to a file, overwriting its current content.
class ToolWriteContentToFile : public Tool {

 public:

  ToolWriteContentToFile() : Tool(ToolType::kEdit, PermissionType::kWrite) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolEditWriteContentToFileOutput>
  WriteContentToFile(const ToolEditWriteContentToFileInput& input);

};

}  // namespace jiaolong
