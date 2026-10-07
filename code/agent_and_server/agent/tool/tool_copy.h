#pragma once

#include <memory>
#include <string>
#include <utility>

#include "agent/tool/tool.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {

// Input and output for Copy
class ToolCopyInput : public ToolUseInput {
 public:
  const std::string source_path_;
  const std::string destination_path_;
  ToolCopyInput(const std::string& source_path,
      const std::string& destination_path) :
      ToolUseInput(ToolType::kCopy), source_path_(source_path),
      destination_path_(destination_path) {};
};
class ToolCopyOutput : public ToolUseOutput {
 public:
  ToolCopyOutput() : ToolUseOutput(ToolType::kCopy) {}
};

class ToolCopy : public Tool {

 public:

  ToolCopy() : Tool(ToolType::kCopy, PermissionType::kWrite) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolCopyOutput> Copy(
      const ToolCopyInput& input);

};

}  // namespace jiaolong