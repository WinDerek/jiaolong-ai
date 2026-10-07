#pragma once

#include <string>

namespace jiaolong {

enum ToolUseStatusType {
  kSuccess,
  kInvalidInput,
  kExecutionFailure
};

class ToolUseStatus {
 public:
  const long long duration_in_ms_;
  const ToolUseStatusType type_;
  ToolUseStatus(const ToolUseStatusType& type,
      const long long& duration_in_ms) :
          type_(type),
          duration_in_ms_(duration_in_ms) {}
  virtual ~ToolUseStatus() = default;
  virtual bool IsSuccess() = 0;
};

class ToolUseSuccess : public ToolUseStatus {
 public:
  ToolUseSuccess(const long long& duration_in_ms) :
      ToolUseStatus(kSuccess, duration_in_ms) {}
  bool IsSuccess() override { return true; }
};

class ToolUseInvalidInput : public ToolUseStatus {
 public:
  const std::string error_message_;
  ToolUseInvalidInput(const long long& duration_in_ms,
      const std::string& error_message) :
          ToolUseStatus(kInvalidInput, duration_in_ms),
          error_message_(error_message) {}
  bool IsSuccess() override { return false; }
};

class ToolUseExecutionFailure : public ToolUseStatus {
 public:
  const std::string error_message_;
  ToolUseExecutionFailure(const long long& duration_in_ms,
      const std::string& error_message) :
          ToolUseStatus(kExecutionFailure, duration_in_ms),
          error_message_(error_message) {}

  bool IsSuccess() override { return false; }
};

}  // namespace jiaolong
