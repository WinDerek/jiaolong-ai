#pragma once

#include <memory>
#include <string>
#include <utility>

#include "agent/tool/tool.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {

// Input and output for Move
class ToolMoveInput : public ToolUseInput {
 public:
  const std::string source_path_;
  const std::string destination_path_;
  ToolMoveInput(const std::string& source_path,
      const std::string& destination_path) :
      ToolUseInput(ToolType::kMove), source_path_(source_path),
      destination_path_(destination_path) {};
};
class ToolMoveOutput : public ToolUseOutput {
 public:
  ToolMoveOutput() : ToolUseOutput(ToolType::kMove) {}
};

class ToolMove : public Tool {

 public:

  ToolMove() : Tool(ToolType::kMove, PermissionType::kWrite) {}

  std::pair<std::shared_ptr<ToolUseStatus>, ToolMoveOutput> Move(
      const ToolMoveInput& input);

};

}  // namespace jiaolong
