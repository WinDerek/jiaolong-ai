#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "agent/tool/tool_find.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {
namespace {

class ToolFindTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static int test_id = 0;
    root_dir_ = std::filesystem::temp_directory_path() /
        ("jiaolong_tool_find_test_" + std::to_string(++test_id));
    std::filesystem::create_directories(root_dir_ / "sub");
    WriteTextFile(root_dir_ / "a.txt", "hello world\nfoo\nhello again\n");
    WriteTextFile(root_dir_ / "sub" / "b.txt", "hello\n");
    WriteTextFile(root_dir_ / "sub" / "c.md", "Jiaolong\n");
    WriteTextFile(root_dir_ / "README.md", "Jiaolong project\nno match here\n");
    WriteBinaryFile(root_dir_ / "binary.bin", {'\x00', '\x01', '\x02'});
  }

  void TearDown() override {
    std::filesystem::remove_all(root_dir_);
  }

  void WriteTextFile(const std::filesystem::path& file_path,
      const std::string& content) {
    std::ofstream file(file_path);
    file << content;
  }

  void WriteBinaryFile(const std::filesystem::path& file_path,
      const std::vector<char>& content) {
    std::ofstream file(file_path, std::ios::binary);
    file.write(content.data(), content.size());
  }

  std::filesystem::path root_dir_;
};

TEST_F(ToolFindTest, FindFilesMatchesFileNameQuery) {
  ToolFindFiles tool_find;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindFilesOutput>
      status_and_output = tool_find.FindFiles(
          ToolFindFindFilesInput(root_dir_.string(), "txt"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  std::vector<std::string> expected_present_files = {
      (root_dir_ / "a.txt").string(),
      (root_dir_ / "sub" / "b.txt").string()};
  // The filesystem iteration order is unspecified, so compare after sorting.
  std::sort(expected_present_files.begin(), expected_present_files.end());
  EXPECT_EQ(status_and_output.second.present_files_, expected_present_files);
}

TEST_F(ToolFindTest, FindFilesWithEmptyQueryIsInvalidInput) {
  ToolFindFiles tool_find;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindFilesOutput>
      status_and_output = tool_find.FindFiles(
          ToolFindFindFilesInput(root_dir_.string(), ""));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
  EXPECT_TRUE(status_and_output.second.present_files_.empty());
}

TEST_F(ToolFindTest, FindFilesWithNoMatchReturnsEmptyList) {
  ToolFindFiles tool_find;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindFilesOutput>
      status_and_output = tool_find.FindFiles(
          ToolFindFindFilesInput(root_dir_.string(), "nonexistent_query"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_TRUE(status_and_output.second.present_files_.empty());
}

TEST_F(ToolFindTest, FindFilesFindsFilesAtAnyDepth) {
  std::filesystem::create_directories(root_dir_ / "sub" / "deep" / "deeper");
  // c.txt is at depth 2 (sub/deep), d.txt is at depth 3 (sub/deep/deeper).
  WriteTextFile(root_dir_ / "sub" / "deep" / "c.txt", "x\n");
  WriteTextFile(root_dir_ / "sub" / "deep" / "deeper" / "d.txt", "x\n");
  ToolFindFiles tool_find;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindFilesOutput>
      status_and_output = tool_find.FindFiles(
          ToolFindFindFilesInput(root_dir_.string(), "txt"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  const std::vector<std::string>& present_files =
      status_and_output.second.present_files_;
  EXPECT_NE(std::find(present_files.begin(), present_files.end(),
      (root_dir_ / "sub" / "deep" / "c.txt").string()),
      present_files.end());
  EXPECT_NE(std::find(present_files.begin(), present_files.end(),
      (root_dir_ / "sub" / "deep" / "deeper" / "d.txt").string()),
      present_files.end());
}

TEST_F(ToolFindTest, FindFilesSkipsIgnoredDirectories) {
  std::filesystem::create_directories(root_dir_ / "node_modules" / "pkg");
  std::filesystem::create_directories(root_dir_ / ".git");
  std::filesystem::create_directories(root_dir_ / "build" / "obj");
  WriteTextFile(root_dir_ / "node_modules" / "pkg" / "index.js", "x\n");
  WriteTextFile(root_dir_ / ".git" / "status", "x\n");
  WriteTextFile(root_dir_ / "build" / "obj" / "src.o", "x\n");
  WriteTextFile(root_dir_ / "src.cpp", "x\n");
  ToolFindFiles tool_find;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindFilesOutput>
      status_and_output = tool_find.FindFiles(
          ToolFindFindFilesInput(root_dir_.string(), "s"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  const std::vector<std::string>& present_files =
      status_and_output.second.present_files_;
  EXPECT_NE(std::find(present_files.begin(), present_files.end(),
      (root_dir_ / "src.cpp").string()), present_files.end());
  EXPECT_EQ(std::find(present_files.begin(), present_files.end(),
      (root_dir_ / "node_modules" / "pkg" / "index.js").string()),
      present_files.end());
  EXPECT_EQ(std::find(present_files.begin(), present_files.end(),
      (root_dir_ / ".git" / "status").string()), present_files.end());
  EXPECT_EQ(std::find(present_files.begin(), present_files.end(),
      (root_dir_ / "build" / "obj" / "src.o").string()),
      present_files.end());
}

TEST_F(ToolFindTest, FindFilesWithInvalidRootPath) {
  ToolFindFiles tool_find;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindFilesOutput>
      status_and_output = tool_find.FindFiles(
          ToolFindFindFilesInput("", "query"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);

  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindFilesOutput>
      status_and_output_nonexistent = tool_find.FindFiles(
          ToolFindFindFilesInput((root_dir_ / "does_not_exist").string(), "query"));
  EXPECT_EQ(status_and_output_nonexistent.first->type_, ToolUseStatusType::kInvalidInput);
}

TEST_F(ToolFindTest, FindStringsReportsLineNumbers) {
  ToolFindStrings tool_find;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindStringsOutput>
      status_and_output = tool_find.FindStrings(
          ToolFindFindStringsInput(root_dir_.string(), "hello"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  const std::vector<ToolFindFileWithLineNumbers> expected_present_files = {
      ToolFindFileWithLineNumbers((root_dir_ / "a.txt").string(), {1, 3}),
      ToolFindFileWithLineNumbers((root_dir_ / "sub" / "b.txt").string(), {1})};
  EXPECT_EQ(status_and_output.second.present_files_, expected_present_files);
}

TEST_F(ToolFindTest, FindStringsMatchesFilesInSubdirectories) {
  ToolFindStrings tool_find;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindStringsOutput>
      status_and_output = tool_find.FindStrings(
          ToolFindFindStringsInput(root_dir_.string(), "Jiaolong"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  const std::vector<ToolFindFileWithLineNumbers> expected_present_files = {
      ToolFindFileWithLineNumbers((root_dir_ / "README.md").string(), {1}),
      ToolFindFileWithLineNumbers((root_dir_ / "sub" / "c.md").string(), {1})};
  EXPECT_EQ(status_and_output.second.present_files_, expected_present_files);
}

TEST_F(ToolFindTest, FindStringsSkipsBinaryFiles) {
  ToolFindStrings tool_find;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindStringsOutput>
      status_and_output = tool_find.FindStrings(
          ToolFindFindStringsInput(root_dir_.string(), "\x01"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  // binary.bin contains the query, but binary files are skipped.
  EXPECT_TRUE(status_and_output.second.present_files_.empty());
}

TEST_F(ToolFindTest, FindStringsWithEmptyQueryIsInvalidInput) {
  ToolFindStrings tool_find;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindStringsOutput>
      status_and_output = tool_find.FindStrings(
          ToolFindFindStringsInput(root_dir_.string(), ""));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
}

TEST_F(ToolFindTest, FindStringsFindsFilesAtAnyDepth) {
  std::filesystem::create_directories(root_dir_ / "sub" / "deep" / "deeper");
  WriteTextFile(root_dir_ / "sub" / "deep" / "c.txt", "hello\n");
  WriteTextFile(root_dir_ / "sub" / "deep" / "deeper" / "d.txt", "hello\n");
  ToolFindStrings tool_find;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindStringsOutput>
      status_and_output = tool_find.FindStrings(
          ToolFindFindStringsInput(root_dir_.string(), "hello"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  const std::vector<ToolFindFileWithLineNumbers>& present_files =
      status_and_output.second.present_files_;
  EXPECT_NE(std::find_if(present_files.begin(), present_files.end(),
      [&](const ToolFindFileWithLineNumbers& present_file) {
        return present_file.file_path_ ==
            (root_dir_ / "sub" / "deep" / "c.txt").string();
      }), present_files.end());
  EXPECT_NE(std::find_if(present_files.begin(), present_files.end(),
      [&](const ToolFindFileWithLineNumbers& present_file) {
        return present_file.file_path_ ==
            (root_dir_ / "sub" / "deep" / "deeper" / "d.txt").string();
      }), present_files.end());
}

TEST_F(ToolFindTest, FindStringsSkipsIgnoredDirectories) {
  std::filesystem::create_directories(root_dir_ / "node_modules" / "pkg");
  WriteTextFile(root_dir_ / "node_modules" / "pkg" / "index.js", "hello\n");
  ToolFindStrings tool_find;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindStringsOutput>
      status_and_output = tool_find.FindStrings(
          ToolFindFindStringsInput(root_dir_.string(), "hello"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  // Only a.txt and sub/b.txt contain "hello"; the node_modules file must be
  // skipped.
  const std::vector<ToolFindFileWithLineNumbers>& present_files =
      status_and_output.second.present_files_;
  EXPECT_EQ(present_files.size(), 2);
  EXPECT_EQ(std::find_if(present_files.begin(), present_files.end(),
      [&](const ToolFindFileWithLineNumbers& present_file) {
        return present_file.file_path_ ==
            (root_dir_ / "node_modules" / "pkg" / "index.js").string();
      }), present_files.end());
}

TEST_F(ToolFindTest, FindStringsWithInvalidRootPath) {
  ToolFindStrings tool_find;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolFindFindStringsOutput>
      status_and_output = tool_find.FindStrings(
          ToolFindFindStringsInput("", "query"));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
}

}  // namespace
}  // namespace jiaolong
