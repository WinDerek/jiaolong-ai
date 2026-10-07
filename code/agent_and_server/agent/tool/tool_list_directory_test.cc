#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "agent/tool/tool_list_directory.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {
namespace {

class ToolListDirectoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static int test_id = 0;
    root_dir_ = std::filesystem::temp_directory_path() /
        ("jiaolong_tool_list_directory_test_" + std::to_string(++test_id));
    std::filesystem::create_directories(root_dir_ / "sub");
    std::filesystem::create_directories(root_dir_ / "sub" / "nested");
    WriteTextFile(root_dir_ / "a.txt", "hello\n");
    WriteTextFile(root_dir_ / "sub" / "b.txt", "hello\n");
    WriteTextFile(root_dir_ / "sub" / "nested" / "c.txt", "hello\n");
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

TEST_F(ToolListDirectoryTest, ListsOnlyImmediateEntries) {
  ToolListDirectory tool_list_directory;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolListDirectoryOutput>
      status_and_output = tool_list_directory.ListDirectory(
          ToolListDirectoryInput(root_dir_.string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  const std::vector<ToolListDirectoryEntry> expected_entries = {
      ToolListDirectoryEntry("a.txt", false),
      ToolListDirectoryEntry("sub", true)};
  EXPECT_EQ(status_and_output.second.entries_, expected_entries);
}

TEST_F(ToolListDirectoryTest, ListsNestedDirectoryContents) {
  ToolListDirectory tool_list_directory;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolListDirectoryOutput>
      status_and_output = tool_list_directory.ListDirectory(
          ToolListDirectoryInput((root_dir_ / "sub").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  const std::vector<ToolListDirectoryEntry> expected_entries = {
      ToolListDirectoryEntry("b.txt", false),
      ToolListDirectoryEntry("nested", true)};
  EXPECT_EQ(status_and_output.second.entries_, expected_entries);
}

TEST_F(ToolListDirectoryTest, ListsEmptyDirectory) {
  std::filesystem::create_directories(root_dir_ / "empty");
  ToolListDirectory tool_list_directory;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolListDirectoryOutput>
      status_and_output = tool_list_directory.ListDirectory(
          ToolListDirectoryInput((root_dir_ / "empty").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_TRUE(status_and_output.second.entries_.empty());
}

TEST_F(ToolListDirectoryTest, WithInvalidDirectoryPath) {
  ToolListDirectory tool_list_directory;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolListDirectoryOutput>
      status_and_output = tool_list_directory.ListDirectory(
          ToolListDirectoryInput(""));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);

  std::pair<std::shared_ptr<ToolUseStatus>, ToolListDirectoryOutput>
      status_and_output_nonexistent = tool_list_directory.ListDirectory(
          ToolListDirectoryInput((root_dir_ / "does_not_exist").string()));
  EXPECT_EQ(status_and_output_nonexistent.first->type_,
      ToolUseStatusType::kInvalidInput);

  // A file is not a directory.
  std::pair<std::shared_ptr<ToolUseStatus>, ToolListDirectoryOutput>
      status_and_output_file = tool_list_directory.ListDirectory(
          ToolListDirectoryInput((root_dir_ / "a.txt").string()));
  EXPECT_EQ(status_and_output_file.first->type_,
      ToolUseStatusType::kInvalidInput);
}

}  // namespace
}  // namespace jiaolong
