#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "agent/tool/tool_edit.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {
namespace {

class ToolEditTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static int test_id = 0;
    root_dir_ = std::filesystem::temp_directory_path() /
        ("jiaolong_tool_edit_test_" + std::to_string(++test_id));
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

TEST_F(ToolEditTest, CreateNewFileWithEmptyContent) {
  const std::filesystem::path file_path = root_dir_ / "empty.txt";
  ToolCreateNewFile tool_edit;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolEditCreateNewFileOutput>
      status_and_output = tool_edit.CreateNewFile(
          ToolEditCreateNewFileInput(file_path.string(), ""));

  // Creating a file with empty content must succeed.
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);

  // The file must exist and be empty.
  std::error_code error_code;
  EXPECT_TRUE(std::filesystem::exists(file_path, error_code));
  ASSERT_FALSE(error_code);
  EXPECT_TRUE(std::filesystem::is_regular_file(file_path, error_code));
  ASSERT_FALSE(error_code);
  EXPECT_EQ(std::filesystem::file_size(file_path, error_code), 0u);
  ASSERT_FALSE(error_code);
}

TEST_F(ToolEditTest, WriteContentToFileWritesContent) {
  const std::filesystem::path file_path = root_dir_ / "a.txt";
  ToolWriteContentToFile tool_edit;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolEditWriteContentToFileOutput>
      status_and_output = tool_edit.WriteContentToFile(
          ToolEditWriteContentToFileInput(file_path.string(), "new content\n"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  std::ifstream file(file_path);
  std::string content((std::istreambuf_iterator<char>(file)),
      std::istreambuf_iterator<char>());
  EXPECT_EQ(content, "new content\n");
}

TEST_F(ToolEditTest, WriteContentToFileWithExistingDirectory) {
  const std::filesystem::path dir_path = root_dir_ / "existing_dir";
  std::filesystem::create_directories(dir_path);

  ToolWriteContentToFile tool_edit;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolEditWriteContentToFileOutput>
      status_and_output = tool_edit.WriteContentToFile(
          ToolEditWriteContentToFileInput(dir_path.string(), "content\n"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
  std::shared_ptr<ToolUseInvalidInput> tool_use_status_invalid_input = static_pointer_cast<ToolUseInvalidInput>(status_and_output.first);
  EXPECT_FALSE(tool_use_status_invalid_input->error_message_.empty());
  EXPECT_NE(tool_use_status_invalid_input->error_message_.find("directory"),
      std::string::npos);
  // The directory must be left untouched.
  EXPECT_TRUE(std::filesystem::is_directory(dir_path));
}

TEST_F(ToolEditTest, WriteContentToFileWithEmptyPath) {
  ToolWriteContentToFile tool_edit;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolEditWriteContentToFileOutput>
      status_and_output = tool_edit.WriteContentToFile(
          ToolEditWriteContentToFileInput("", "content\n"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
}

TEST_F(ToolEditTest, SearchAndReplaceWithBackslashBackslashNInOldStr) {
  // The old string contains the literal character sequence backslash-backslash-n.
  const std::string old_str = "Task title: `{}`.\\\\nTask description:";
  const std::string new_str = "Task title: `{}`.\\\\nTask description: replaced";
  const std::string prefix = "line 1\n";
  const std::string suffix = "\nline 3\n";

  // The content described above is only a part of a complete file.
  const std::filesystem::path file_path = root_dir_ / "task.txt";
  WriteTextFile(file_path, prefix + old_str + suffix);

  ToolSearchAndReplace tool_edit;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolEditSearchAndReplaceOutput>
      status_and_output = tool_edit.SearchAndReplace(
          ToolEditSearchAndReplaceInput(file_path.string(), old_str, new_str));

  // SearchAndReplace must succeed even when old_str contains `\\n`; a failure
  // here demonstrates the suspected bug.
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);

  // The file must actually contain the replacement text.
  std::ifstream file(file_path);
  std::string content((std::istreambuf_iterator<char>(file)),
      std::istreambuf_iterator<char>());
  EXPECT_EQ(content, prefix + new_str + suffix);
}

}  // namespace
}  // namespace jiaolong
