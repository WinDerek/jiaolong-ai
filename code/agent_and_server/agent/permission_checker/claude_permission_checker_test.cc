#include <gtest/gtest.h>

#include "agent/permission_checker/claude_permission_checker.h"

TEST(JiaolongPermissionCheckerTest, WorkingDirectoryNotAllowed) {
  jiaolong::ClaudePermissionChecker claude_permission_checker;
  const nlohmann::json parameters_json = nlohmann::json::parse(R"(
    {
      "tool_name": "Bash",
      "cwd": "/home/user/not_allowed_working_directory"
    }
  )");
  jiaolong::PermissionCheckResult result = claude_permission_checker.CheckPermission(
      parameters_json);
  EXPECT_EQ(result.allowed, false);
}

TEST(JiaolongPermissionCheckerTest, ToolNotAllowed) {
  jiaolong::ClaudePermissionChecker claude_permission_checker;
  const nlohmann::json parameters_json = nlohmann::json::parse(R"(
    {
      "tool_name": "AskUserQuestion",
      "cwd": "/home/user/workspace"
    }
  )");
  jiaolong::PermissionCheckResult result = claude_permission_checker.CheckPermission(
      parameters_json);
  EXPECT_EQ(result.allowed, false);
}

TEST(JiaolongPermissionCheckerTest, EditNotAllowed) {
  jiaolong::ClaudePermissionChecker permission_checker;
  nlohmann::json parameters_json = nlohmann::json::parse(R"(
    {
      "tool_name": "Edit",
      "cwd": "/home/user/workspace",
      "tool_input": {
        "file_path": "/home/user/workspace/README.md"
      }
    }
  )");
  jiaolong::PermissionCheckResult result = permission_checker.CheckPermission(
      parameters_json);
  EXPECT_EQ(result.allowed, false);
}

TEST(JiaolongPermissionCheckerTest, EditAllowed) {
  jiaolong::ClaudePermissionChecker permission_checker;
  const nlohmann::json parameters_json = nlohmann::json::parse(R"(
    {
      "tool_name": "Edit",
      "cwd": "/home/user/workspace",
      "tool_input": {
        "file_path": "/home/user/workspace/doc/test.md"
      }
    }
  )");
  jiaolong::PermissionCheckResult result = permission_checker.CheckPermission(
      parameters_json);
  EXPECT_EQ(result.allowed, true);
}

TEST(JiaolongPermissionCheckerTest, ReadNotAllowed) {
  jiaolong::ClaudePermissionChecker permission_checker;
  nlohmann::json parameters_json = nlohmann::json::parse(R"(
    {
      "tool_name": "Read",
      "cwd": "/home/user/workspace",
      "tool_input": {
        "file_path": "/home/user/workspace/README.md"
      }
    }
  )");
  jiaolong::PermissionCheckResult result = permission_checker.CheckPermission(
      parameters_json);
  EXPECT_EQ(result.allowed, false);
}

TEST(JiaolongPermissionCheckerTest, ReadAllowed) {
  jiaolong::ClaudePermissionChecker permission_checker;
  const nlohmann::json parameters_json = nlohmann::json::parse(R"(
    {
      "tool_name": "Read",
      "cwd": "/home/user/workspace",
      "tool_input": {
        "file_path": "/home/user/workspace/doc/test.md"
      }
    }
  )");
  jiaolong::PermissionCheckResult result = permission_checker.CheckPermission(
      parameters_json);
  EXPECT_EQ(result.allowed, true);
}

TEST(JiaolongPermissionCheckerTest, ListDirectoryNotAllowed) {
  jiaolong::ClaudePermissionChecker permission_checker;
  const nlohmann::json parameters_json = nlohmann::json::parse(R"(
    {
      "tool_name": "Bash",
      "cwd": "/home/user/workspace",
      "tool_input": {
        "command": "ls /home/user/workspace"
      }
    }
  )");
  jiaolong::PermissionCheckResult result = permission_checker.CheckPermission(
      parameters_json);
  EXPECT_EQ(result.allowed, false);
}

TEST(JiaolongPermissionCheckerTest, ListDirectoryAllowed) {
  jiaolong::ClaudePermissionChecker permission_checker;
  const nlohmann::json parameters_json = nlohmann::json::parse(R"(
    {
      "tool_name": "Bash",
      "cwd": "/home/user/workspace",
      "tool_input": {
        "command": "ls /home/user/workspace/doc"
      }
    }
  )");
  jiaolong::PermissionCheckResult result = permission_checker.CheckPermission(
      parameters_json);
  EXPECT_EQ(result.allowed, true);
}