#include <vector>
#include <filesystem>

#include <gtest/gtest.h>

#include "agent/permission_checker/bash_command_permission_checker.h"
#include "agent/permission_checker/permission_check_result.h"

TEST(BashCommandPermissionCheckerTest, ExecutableNotAllowed) {
  std::vector<std::filesystem::path> allowed_paths = {};
  std::filesystem::path working_directory_path = "/home/root/workspace";
  jiaolong::BashCommandPermissionChecker checker(allowed_paths);
  jiaolong::PermissionCheckResult result = checker.CheckCommand(
      "docker image ls --all", working_directory_path);
  EXPECT_EQ(result.allowed, false);
  EXPECT_EQ(result.reason, "Executable not allowed, executable: \"docker\"");
}

TEST(BashCommandPermissionCheckerTest, ListDirectoryRelativePathNotAllowed) {
  std::vector<std::filesystem::path> allowed_paths = {
    "/home/root/workspace/jiaolong/plan"
  };
  std::filesystem::path working_directory_path = "/home/root/workspace";
  jiaolong::BashCommandPermissionChecker checker(allowed_paths);
  jiaolong::PermissionCheckResult result = checker.CheckCommand(
      "ls -la workspace/jiaolong/summary", working_directory_path);
  EXPECT_EQ(result.allowed, false);
}

TEST(BashCommandPermissionCheckerTest, ListDirectoryAbsolutePathNotAllowed) {
  std::vector<std::filesystem::path> allowed_paths = {
    "/home/root/workspace/jiaolong/plan"
  };
  std::filesystem::path working_directory_path = "/home/root/workspace";
  jiaolong::BashCommandPermissionChecker checker(allowed_paths);
  jiaolong::PermissionCheckResult result = checker.CheckCommand(
      "ls -la /home/root/workspace/jiaolong/summary", working_directory_path);
  EXPECT_EQ(result.allowed, false);
}

TEST(BashCommandPermissionCheckerTest, ListDirectoryAbsoluteAllowed) {
  std::vector<std::filesystem::path> allowed_paths = {
    "/home/root/workspace/jiaolong/plan"
  };
  std::filesystem::path working_directory_path = "/home/root/workspace";
  jiaolong::BashCommandPermissionChecker checker(allowed_paths);
  jiaolong::PermissionCheckResult result = checker.CheckCommand(
      "ls /home/root/workspace/jiaolong/plan", working_directory_path);
  EXPECT_EQ(result.allowed, true);
}

TEST(BashCommandPermissionCheckerTest, ListDirectoryRelativeAllowed) {
  std::vector<std::filesystem::path> allowed_paths = {
    "/home/root/workspace/jiaolong/plan"
  };
  std::filesystem::path working_directory_path = "/home/root/workspace";
  jiaolong::BashCommandPermissionChecker checker(allowed_paths);
  jiaolong::PermissionCheckResult result = checker.CheckCommand(
      "ls jiaolong/plan", working_directory_path);
  EXPECT_EQ(result.allowed, true);
}

TEST(BashCommandPermissionCheckerTest, ListDirectoryRelativeWithDotAllowed) {
  std::vector<std::filesystem::path> allowed_paths = {
    "/home/root/workspace/jiaolong/plan"
  };
  std::filesystem::path working_directory_path = "/home/root/workspace";
  jiaolong::BashCommandPermissionChecker checker(allowed_paths);
  jiaolong::PermissionCheckResult result = checker.CheckCommand(
      "ls ./jiaolong/plan", working_directory_path);
  EXPECT_EQ(result.allowed, true);
}
