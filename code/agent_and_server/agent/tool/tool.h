#pragma once

#include <string>
#include <utility>

#include "agent/tool/tool_use_status.h"

namespace jiaolong {

enum ToolType {
  kRead,
  kEdit,
  kDelete,
  kMove,
  kCopy,
  kFind,
  kListDirectory,
  kBash,
  kVcs,
  kManageProject
};

class ToolUseInput {
 public:
  ToolUseInput(const ToolType& tool_type) : tool_type_(tool_type) {}
  virtual ~ToolUseInput() = default;
 private:
  const ToolType tool_type_;
};

class ToolUseOutput {
 public:
  ToolUseOutput(const ToolType& tool_type) : tool_type_(tool_type) {}
  virtual ~ToolUseOutput() = default;
 private:
  const ToolType tool_type_;
};

class Tool {

 public:

  // Whether a tool only reads files or may also write to them. Only tools with
  // the write permission type are checked against the forbidden-to-write file
  // list before they run, so read-only tools never fail that check.
  enum PermissionType {
    kRead,
    kWrite
  };

  Tool(const ToolType& tool_type, const PermissionType& permission_type) :
      tool_type_(tool_type),
      permission_type_(permission_type) {};

  virtual ~Tool() = default;

  ToolType GetToolType() {
    return tool_type_;
  }

  PermissionType GetPermissionType() const {
    return permission_type_;
  }

 private:

  const ToolType tool_type_;

  const PermissionType permission_type_;

};

// Allows the permission type to be referred to without the `Tool::` qualifier,
// e.g. `PermissionType::kWrite`.
using PermissionType = Tool::PermissionType;

}  // namespace jiaolong
