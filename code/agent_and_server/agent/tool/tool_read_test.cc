#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "agent/tool/tool_read.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {
namespace {

class ToolReadTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static int test_id = 0;
    root_dir_ = std::filesystem::temp_directory_path() /
        ("jiaolong_tool_read_test_" + std::to_string(++test_id));
    std::filesystem::create_directories(root_dir_);
    WriteTextFile(root_dir_ / "a.txt", "hello world\nsecond line\n");
    WriteBinaryFile(root_dir_ / "binary.bin", {'\x00', '\x01', '\x02'});
    WriteBinaryFile(root_dir_ / "binary_with_nul.bin",
        std::vector<char>{'a', 'b', 'c', '\0', 'd', 'e', 'f'});
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

TEST_F(ToolReadTest, ReadWholeFileReturnsTextContent) {
  ToolReadWholeFile tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadWholeFileOutput>
      status_and_output = tool_read.ReadWholeFile(
          ToolReadReadWholeFileInput((root_dir_ / "a.txt").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_EQ(status_and_output.second.file_content_, "hello world\nsecond line\n");
}

TEST_F(ToolReadTest, ReadWholeFileReturnsBinaryMarkerForBinaryFile) {
  ToolReadWholeFile tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadWholeFileOutput>
      status_and_output = tool_read.ReadWholeFile(
          ToolReadReadWholeFileInput((root_dir_ / "binary.bin").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_EQ(status_and_output.second.file_content_, "binary file");
}

TEST_F(ToolReadTest, ReadWholeFileReturnsBinaryMarkerForTextWithNulBytes) {
  ToolReadWholeFile tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadWholeFileOutput>
      status_and_output = tool_read.ReadWholeFile(
          ToolReadReadWholeFileInput(
              (root_dir_ / "binary_with_nul.bin").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_EQ(status_and_output.second.file_content_, "binary file");
}

TEST_F(ToolReadTest, ReadWholeFileWithInvalidPath) {
  ToolReadWholeFile tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadWholeFileOutput>
      status_and_output = tool_read.ReadWholeFile(
          ToolReadReadWholeFileInput(
              (root_dir_ / "does_not_exist.txt").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kInvalidInput);
}

TEST_F(ToolReadTest, CountLinesReturnsLineCount) {
  ToolCountLines tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadCountLinesOutput>
      status_and_output = tool_read.CountLines(
          ToolReadCountLinesInput((root_dir_ / "a.txt").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_EQ(status_and_output.second.num_lines_, 2);
}

TEST_F(ToolReadTest, ReadFileOfLineRangeReturnsTextContent) {
  ToolReadFileOfLineRange tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadFileOfLineRangeOutput>
      status_and_output = tool_read.ReadFileOfLineRange(
          ToolReadReadFileOfLineRangeInput(
              (root_dir_ / "a.txt").string(), 1, 1));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_EQ(status_and_output.second.file_content_, "hello world\n");
}

TEST_F(ToolReadTest, ReadFileOfLineRangeReturnsBinaryMarkerForBinaryFile) {
  ToolReadFileOfLineRange tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadFileOfLineRangeOutput>
      status_and_output = tool_read.ReadFileOfLineRange(
          ToolReadReadFileOfLineRangeInput(
              (root_dir_ / "binary.bin").string(), 1, 10));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_EQ(status_and_output.second.file_content_, "binary file");
}

TEST_F(ToolReadTest, ReadFileOfLineRangeDetectsBinaryOutsideRequestedRange) {
  // The NUL byte is on the second line, outside the requested line range,
  // so the whole file must be checked, not just the returned lines.
  const std::string mixed_content("first line\nsecond \0line\n", 24);
  WriteBinaryFile(root_dir_ / "mixed.bin",
      std::vector<char>(mixed_content.begin(), mixed_content.end()));
  ToolReadFileOfLineRange tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadFileOfLineRangeOutput>
      status_and_output = tool_read.ReadFileOfLineRange(
          ToolReadReadFileOfLineRangeInput(
              (root_dir_ / "mixed.bin").string(), 1, 1));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_EQ(status_and_output.second.file_content_, "binary file");
}

TEST_F(ToolReadTest, ReadWholeFileReturnsFullContentWhenAtLineLimit) {
  // A file with exactly 1000 lines must not be truncated.
  std::string content;
  for (int i = 1; i <= 1000; ++i) {
    content += "line " + std::to_string(i) + "\n";
  }
  WriteTextFile(root_dir_ / "at_limit.txt", content);
  ToolReadWholeFile tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadWholeFileOutput>
      status_and_output = tool_read.ReadWholeFile(
          ToolReadReadWholeFileInput((root_dir_ / "at_limit.txt").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_EQ(status_and_output.second.file_content_, content);
}

TEST_F(ToolReadTest, ReadWholeFileTruncatesWhenExceedingLineLimit) {
  // A file with 1001 lines must be truncated to 1000 lines with an indicator.
  std::string content;
  for (int i = 1; i <= 1001; ++i) {
    content += "line " + std::to_string(i) + "\n";
  }
  WriteTextFile(root_dir_ / "exceeds_limit.txt", content);
  ToolReadWholeFile tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadWholeFileOutput>
      status_and_output = tool_read.ReadWholeFile(
          ToolReadReadWholeFileInput(
              (root_dir_ / "exceeds_limit.txt").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);

  std::string expected;
  for (int i = 1; i <= 1000; ++i) {
    expected += "line " + std::to_string(i) + "\n";
  }
  expected += "... truncated due to lines limit (max 1000).";
  EXPECT_EQ(status_and_output.second.file_content_, expected);
}

TEST_F(ToolReadTest, ReadWholeFileKeepsLastLineWithoutTrailingNewlineAtLimit) {
  // Exactly 1000 lines where the last line has no trailing newline must not
  // be truncated or modified.
  std::string content;
  for (int i = 1; i <= 999; ++i) {
    content += "line " + std::to_string(i) + "\n";
  }
  content += "line 1000";
  WriteTextFile(root_dir_ / "at_limit_no_trailing_newline.txt", content);
  ToolReadWholeFile tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadWholeFileOutput>
      status_and_output = tool_read.ReadWholeFile(
          ToolReadReadWholeFileInput(
              (root_dir_ / "at_limit_no_trailing_newline.txt").string()));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_EQ(status_and_output.second.file_content_, content);
}

TEST_F(ToolReadTest, ReadFileOfLineRangeReturnsFullContentWhenWithinLineLimit) {
  // A range of exactly 1000 lines must not be truncated.
  std::string content;
  for (int i = 1; i <= 1000; ++i) {
    content += "line " + std::to_string(i) + "\n";
  }
  WriteTextFile(root_dir_ / "range_at_limit.txt", content);
  ToolReadFileOfLineRange tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadFileOfLineRangeOutput>
      status_and_output = tool_read.ReadFileOfLineRange(
          ToolReadReadFileOfLineRangeInput(
              (root_dir_ / "range_at_limit.txt").string(), 1, 1000));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);
  EXPECT_EQ(status_and_output.second.file_content_, content);
}

TEST_F(ToolReadTest, ReadFileOfLineRangeTruncatesWhenExceedingLineLimit) {
  // A range spanning more than 1000 lines must be truncated to the first
  // 1000 lines of the range with an indicator.
  std::string content;
  for (int i = 1; i <= 2000; ++i) {
    content += "line " + std::to_string(i) + "\n";
  }
  WriteTextFile(root_dir_ / "range_exceeds_limit.txt", content);
  ToolReadFileOfLineRange tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadFileOfLineRangeOutput>
      status_and_output = tool_read.ReadFileOfLineRange(
          ToolReadReadFileOfLineRangeInput(
              (root_dir_ / "range_exceeds_limit.txt").string(), 1, 2000));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);

  std::string expected;
  for (int i = 1; i <= 1000; ++i) {
    expected += "line " + std::to_string(i) + "\n";
  }
  expected += "... truncated due to lines limit (max 1000).";
  EXPECT_EQ(status_and_output.second.file_content_, expected);
}

TEST_F(ToolReadTest, ReadFileOfLineRangeTruncatesFromRangeStart) {
  // When a range starts beyond line 1, truncation keeps the first 1000 lines
  // of the requested range, not the first 1000 lines of the file.
  std::string content;
  for (int i = 1; i <= 2000; ++i) {
    content += "line " + std::to_string(i) + "\n";
  }
  WriteTextFile(root_dir_ / "range_starts_later.txt", content);
  ToolReadFileOfLineRange tool_read;
  std::pair<std::shared_ptr<ToolUseStatus>, ToolReadReadFileOfLineRangeOutput>
      status_and_output = tool_read.ReadFileOfLineRange(
          ToolReadReadFileOfLineRangeInput(
              (root_dir_ / "range_starts_later.txt").string(), 500, 2000));
  EXPECT_EQ(status_and_output.first->type_, ToolUseStatusType::kSuccess);

  std::string expected;
  for (int i = 500; i <= 1499; ++i) {
    expected += "line " + std::to_string(i) + "\n";
  }
  expected += "... truncated due to lines limit (max 1000).";
  EXPECT_EQ(status_and_output.second.file_content_, expected);
}

}  // namespace
}  // namespace jiaolong
