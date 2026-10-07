#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "agent/tool/tool_delete.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {
namespace {

class ToolDeleteTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static int test_id = 0;
    root_dir_ = std::filesystem::temp_directory_path() /
        ("jiaolong_tool_delete_test_" + std::to_string(++test_id));
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

TEST_F(ToolDeleteTest, DeletesSingleFile) {
  const std::filesystem::path file_path = root_dir_ / "a.txt";
  WriteTextFile(file_path, "hello\n");

  ToolDelete tool_delete;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolDeleteOutput>
      status_and_output = tool_delete.Delete(ToolDeleteInput(file_path.string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_FALSE(std::filesystem::exists(file_path));
}

TEST_F(ToolDeleteTest, DeletesDirectoryRecursively) {
  const std::filesystem::path dir_path = root_dir_ / "sub";
  std::filesystem::create_directories(dir_path / "nested");
  WriteTextFile(dir_path / "b.txt", "hello\n");
  WriteTextFile(dir_path / "nested" / "c.txt", "hello\n");

  ToolDelete tool_delete;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolDeleteOutput>
      status_and_output = tool_delete.Delete(ToolDeleteInput(dir_path.string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_FALSE(std::filesystem::exists(dir_path));
}

TEST_F(ToolDeleteTest, WithEmptyPath) {
  ToolDelete tool_delete;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolDeleteOutput>
      status_and_output = tool_delete.Delete(ToolDeleteInput(""));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
}

TEST_F(ToolDeleteTest, WithNonexistentPath) {
  ToolDelete tool_delete;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolDeleteOutput>
      status_and_output = tool_delete.Delete(
          ToolDeleteInput((root_dir_ / "does_not_exist").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
  EXPECT_FALSE(std::filesystem::exists(root_dir_ / "does_not_exist"));
}

}  // namespace
}  // namespace jiaolong
