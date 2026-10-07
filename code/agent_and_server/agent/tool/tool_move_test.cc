#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "agent/tool/tool_move.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {
namespace {

class ToolMoveTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static int test_id = 0;
    root_dir_ = std::filesystem::temp_directory_path() /
        ("jiaolong_tool_move_test_" + std::to_string(++test_id));
    std::filesystem::create_directories(root_dir_);
  }

  void TearDown() override {
    std::filesystem::remove_all(root_dir_);
  }

  void WriteTextFile(const std::filesystem::path& file_path,
      const std::string& content) {
    std::ofstream file(file_path);
    file << content;
  }

  std::filesystem::path root_dir_;
};

TEST_F(ToolMoveTest, MovesSingleFile) {
  const std::filesystem::path source_path = root_dir_ / "a.txt";
  const std::filesystem::path destination_path = root_dir_ / "b.txt";
  WriteTextFile(source_path, "hello\n");

  ToolMove tool_move;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolMoveOutput>
      status_and_output = tool_move.Move(ToolMoveInput(
          source_path.string(), destination_path.string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_FALSE(std::filesystem::exists(source_path));
  EXPECT_TRUE(std::filesystem::exists(destination_path));
}

TEST_F(ToolMoveTest, MovesDirectory) {
  const std::filesystem::path source_dir = root_dir_ / "source";
  const std::filesystem::path destination_dir = root_dir_ / "destination";
  std::filesystem::create_directories(source_dir / "nested");
  WriteTextFile(source_dir / "a.txt", "hello\n");
  WriteTextFile(source_dir / "nested" / "b.txt", "hello\n");

  ToolMove tool_move;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolMoveOutput>
      status_and_output = tool_move.Move(ToolMoveInput(
          source_dir.string(), destination_dir.string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_FALSE(std::filesystem::exists(source_dir));
  EXPECT_TRUE(std::filesystem::exists(destination_dir));
  EXPECT_TRUE(std::filesystem::exists(destination_dir / "nested" / "b.txt"));
}

TEST_F(ToolMoveTest, WithEmptySourcePath) {
  ToolMove tool_move;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolMoveOutput>
      status_and_output = tool_move.Move(
          ToolMoveInput("", (root_dir_ / "b.txt").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
}

TEST_F(ToolMoveTest, WithEmptyDestinationPath) {
  ToolMove tool_move;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolMoveOutput>
      status_and_output = tool_move.Move(
          ToolMoveInput((root_dir_ / "a.txt").string(), ""));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
}

TEST_F(ToolMoveTest, WithNonexistentSourcePath) {
  ToolMove tool_move;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolMoveOutput>
      status_and_output = tool_move.Move(ToolMoveInput(
          (root_dir_ / "does_not_exist").string(),
          (root_dir_ / "b.txt").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
  EXPECT_FALSE(std::filesystem::exists(root_dir_ / "does_not_exist"));
}

}  // namespace
}  // namespace jiaolong
