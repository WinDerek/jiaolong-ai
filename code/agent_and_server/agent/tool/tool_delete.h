#pragma once

#include <memory>
#include <string>
#include <utility>

#include "agent/tool/tool.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {

// Input and output for Delete
class ToolDeleteInput : public ToolUseInput {
 public:
  const std::string path_;
  ToolDeleteInput(const std::string& path) :
      ToolUseInput(ToolType::kDelete), path_(path) {};
};
class ToolDeleteOutput : public ToolUseOutput {
 public:
  ToolDeleteOutput() : ToolUseOutput(ToolType::kDelete) {}
};

class ToolDelete : public Tool {

 public:

  ToolDelete() : Tool(ToolType::kDelete, PermissionType::kWrite) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolDeleteOutput> Delete(
      const ToolDeleteInput& input);

};

}  // namespace jiaolong
