#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "agent/tool/tool.h"

namespace jiaolong {

// A single entry (file or directory) inside the listed directory.
class ToolListDirectoryEntry {
 public:
  std::string name_;
  bool is_directory_;
  ToolListDirectoryEntry(const std::string& name, const bool is_directory) :
      name_(name), is_directory_(is_directory) {}
  bool operator==(const ToolListDirectoryEntry& other) const {
    return name_ == other.name_ && is_directory_ == other.is_directory_;
  }
};

// Input and output for ListDirectory
class ToolListDirectoryInput : public ToolUseInput {
 public:
  const std::string directory_path_;
  ToolListDirectoryInput(const std::string& directory_path) :
      ToolUseInput(ToolType::kListDirectory), directory_path_(directory_path) {};
};
class ToolListDirectoryOutput : public ToolUseOutput {
 public:
  const std::vector<ToolListDirectoryEntry> entries_;
  ToolListDirectoryOutput(const std::vector<ToolListDirectoryEntry>& entries) :
      ToolUseOutput(ToolType::kListDirectory), entries_(entries) {}
};

class ToolListDirectory : public Tool {

 public:

  ToolListDirectory() : Tool(ToolType::kListDirectory, PermissionType::kRead) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolListDirectoryOutput> ListDirectory(const ToolListDirectoryInput& input);

};

}  // namespace jiaolong
