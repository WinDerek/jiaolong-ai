#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "agent/tool/tool_copy.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {
namespace {

class ToolCopyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static int test_id = 0;
    root_dir_ = std::filesystem::temp_directory_path() /
        ("jiaolong_tool_copy_test_" + std::to_string(++test_id));
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

TEST_F(ToolCopyTest, CopiesSingleFile) {
  const std::filesystem::path source_path = root_dir_ / "a.txt";
  const std::filesystem::path destination_path = root_dir_ / "b.txt";
  WriteTextFile(source_path, "hello\n");

  ToolCopy tool_copy;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolCopyOutput>
      status_and_output = tool_copy.Copy(ToolCopyInput(
          source_path.string(), destination_path.string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  // The source must be preserved, and the destination must now exist with the
  // same content.
  EXPECT_TRUE(std::filesystem::exists(source_path));
  EXPECT_TRUE(std::filesystem::exists(destination_path));
  std::ifstream copied_file(destination_path);
  std::stringstream buffer;
  buffer << copied_file.rdbuf();
  EXPECT_EQ(buffer.str(), "hello\n");
}

TEST_F(ToolCopyTest, CopiesDirectory) {
  const std::filesystem::path source_dir = root_dir_ / "source";
  const std::filesystem::path destination_dir = root_dir_ / "destination";
  std::filesystem::create_directories(source_dir / "nested");
  WriteTextFile(source_dir / "a.txt", "hello\n");
  WriteTextFile(source_dir / "nested" / "b.txt", "hello\n");

  ToolCopy tool_copy;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolCopyOutput>
      status_and_output = tool_copy.Copy(ToolCopyInput(
          source_dir.string(), destination_dir.string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  // The source directory must be preserved.
  EXPECT_TRUE(std::filesystem::exists(source_dir));
  EXPECT_TRUE(std::filesystem::exists(destination_dir));
  EXPECT_TRUE(std::filesystem::exists(destination_dir / "a.txt"));
  EXPECT_TRUE(std::filesystem::exists(destination_dir / "nested" / "b.txt"));
}

TEST_F(ToolCopyTest, CopiesFileIntoExistingDirectory) {
  const std::filesystem::path source_path = root_dir_ / "a.txt";
  const std::filesystem::path destination_dir = root_dir_ / "destination";
  std::filesystem::create_directories(destination_dir);
  WriteTextFile(source_path, "hello\n");

  ToolCopy tool_copy;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolCopyOutput>
      status_and_output = tool_copy.Copy(ToolCopyInput(
          source_path.string(), destination_dir.string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_TRUE(std::filesystem::exists(destination_dir / "a.txt"));
}

TEST_F(ToolCopyTest, CopyingOntoSelfIsNoOpSuccess) {
  const std::filesystem::path source_path = root_dir_ / "a.txt";
  WriteTextFile(source_path, "hello\n");

  ToolCopy tool_copy;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolCopyOutput>
      status_and_output = tool_copy.Copy(ToolCopyInput(
          source_path.string(), source_path.string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_TRUE(std::filesystem::exists(source_path));
}

TEST_F(ToolCopyTest, CopyingDirectoryIntoItsOwnSubdirectoryIsInvalid) {
  const std::filesystem::path source_dir = root_dir_ / "source";
  std::filesystem::create_directories(source_dir / "nested");

  ToolCopy tool_copy;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolCopyOutput>
      status_and_output = tool_copy.Copy(ToolCopyInput(
          source_dir.string(),
          (source_dir / "nested" / "copy").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
  EXPECT_FALSE(std::filesystem::exists(source_dir / "nested" / "copy"));
}

TEST_F(ToolCopyTest, WithEmptySourcePath) {
  ToolCopy tool_copy;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolCopyOutput>
      status_and_output = tool_copy.Copy(
          ToolCopyInput("", (root_dir_ / "b.txt").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
}

TEST_F(ToolCopyTest, WithEmptyDestinationPath) {
  ToolCopy tool_copy;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolCopyOutput>
      status_and_output = tool_copy.Copy(
          ToolCopyInput((root_dir_ / "a.txt").string(), ""));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
}

TEST_F(ToolCopyTest, WithNonexistentSourcePath) {
  ToolCopy tool_copy;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolCopyOutput>
      status_and_output = tool_copy.Copy(ToolCopyInput(
          (root_dir_ / "does_not_exist").string(),
          (root_dir_ / "b.txt").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
  EXPECT_FALSE(std::filesystem::exists(root_dir_ / "does_not_exist"));
}

TEST_F(ToolCopyTest, WithDestinationParentDirectoryMissing) {
  const std::filesystem::path source_path = root_dir_ / "a.txt";
  const std::filesystem::path destination_path =
      root_dir_ / "missing_parent" / "b.txt";
  WriteTextFile(source_path, "hello\n");

  // The destination's parent directory does not exist, so the copy operation
  // cannot succeed. This must be reported as an execution failure instead of
  // throwing an exception.
  ToolCopy tool_copy;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolCopyOutput>
      status_and_output = tool_copy.Copy(ToolCopyInput(
          source_path.string(), destination_path.string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kExecutionFailure);
  EXPECT_FALSE(std::filesystem::exists(destination_path));
}

TEST_F(ToolCopyTest, CopyingDirectoryOntoExistingFileFails) {
  const std::filesystem::path source_dir = root_dir_ / "source";
  const std::filesystem::path destination_file = root_dir_ / "dest.txt";
  std::filesystem::create_directories(source_dir);
  WriteTextFile(source_dir / "a.txt", "hello\n");
  WriteTextFile(destination_file, "existing\n");

  // Copying a directory onto an existing regular file is not allowed; it
  // must be reported as an execution failure instead of throwing.
  ToolCopy tool_copy;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolCopyOutput>
      status_and_output = tool_copy.Copy(ToolCopyInput(
          source_dir.string(), destination_file.string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kExecutionFailure);
  // The destination file must be left untouched.
  EXPECT_TRUE(std::filesystem::is_regular_file(destination_file));
  std::ifstream destination(destination_file);
  std::stringstream buffer;
  buffer << destination.rdbuf();
  EXPECT_EQ(buffer.str(), "existing\n");
}

TEST_F(ToolCopyTest, CopiesDirectoryIntoExistingDirectory) {
  const std::filesystem::path source_dir = root_dir_ / "source";
  const std::filesystem::path destination_dir = root_dir_ / "destination";
  std::filesystem::create_directories(source_dir / "nested");
  std::filesystem::create_directories(destination_dir);
  WriteTextFile(source_dir / "a.txt", "hello\n");
  WriteTextFile(source_dir / "nested" / "b.txt", "hello\n");

  // When the destination already exists as a directory, the source directory
  // is copied into it under its own name.
  ToolCopy tool_copy;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolCopyOutput>
      status_and_output = tool_copy.Copy(ToolCopyInput(
          source_dir.string(), destination_dir.string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_TRUE(std::filesystem::exists(destination_dir / "source" / "a.txt"));
  EXPECT_TRUE(std::filesystem::exists(
      destination_dir / "source" / "nested" / "b.txt"));
}

}  // namespace
}  // namespace jiaolong