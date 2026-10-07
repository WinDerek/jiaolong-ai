#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "agent/session_history_visualizer/session_history_visualizer.h"

namespace jiaolong {
namespace {

nlohmann::json CreateSampleSessionHistory() {
  return nlohmann::json::parse(R"({
    "creationTimestamp": "20260811_120956",
    "sessionId": "session_test_uuid",
    "taskId": "task_test_uuid",
    "fileLocation": "/home/user/.jiaolong/sessions/session_test_uuid.json",
    "agentTurns": [
      {
        "type": "message",
        "message": {
          "role": "system",
          "content": "You are an intelligent assistant."
        }
      },
      {
        "type": "message",
        "message": {
          "role": "user",
          "content": "Task: `Fix the bug`."
        }
      },
      {
        "type": "message",
        "message": {
          "role": "assistant",
          "content": "",
          "tool_calls": [
            {
              "id": "call_123",
              "type": "function",
              "function": {
                "name": "SearchAndReplace",
                "arguments": "{\"filePath\":\"/workspace/a.cc\",\"oldStr\":\"a\",\"newStr\":\"b\"}"
              }
            }
          ]
        }
      },
      {
        "type": "message",
        "message": {
          "role": "tool",
          "tool_call_id": "call_123",
          "content": "success"
        }
      },
      {
        "type": "slash_command",
        "command": "/clear"
      }
    ]
  })");
}

TEST(SessionHistoryVisualizerTest, GenerateHtmlContainsCreationTimestamp) {
  SessionHistoryVisualizer visualizer;
  const std::string html = visualizer.GenerateHtml(CreateSampleSessionHistory());
  // The persisted "YYYYMMDD_HHMMSS" timestamp must be displayed as
  // "YYYY-MM-DD HH:mm:SS".
  EXPECT_NE(html.find("2026-08-11 12:09:56"), std::string::npos);
  EXPECT_EQ(html.find("20260811_120956"), std::string::npos);
}

TEST(SessionHistoryVisualizerTest, GenerateHtmlContainsSessionInfo) {
  SessionHistoryVisualizer visualizer;
  const std::string html = visualizer.GenerateHtml(CreateSampleSessionHistory());
  EXPECT_NE(html.find("Session ID:"), std::string::npos);
  EXPECT_NE(html.find("session_test_uuid"), std::string::npos);
  EXPECT_NE(html.find("Task ID:"), std::string::npos);
  EXPECT_NE(html.find("task_test_uuid"), std::string::npos);
  EXPECT_NE(html.find("File location:"), std::string::npos);
  EXPECT_NE(html.find("/home/user/.jiaolong/sessions/session_test_uuid.json"),
      std::string::npos);
}

TEST(SessionHistoryVisualizerTest, GenerateHtmlContainsMessageRoles) {
  SessionHistoryVisualizer visualizer;
  const std::string html = visualizer.GenerateHtml(CreateSampleSessionHistory());
  EXPECT_NE(html.find("system"), std::string::npos);
  EXPECT_NE(html.find("user"), std::string::npos);
  EXPECT_NE(html.find("assistant"), std::string::npos);
  EXPECT_NE(html.find("tool"), std::string::npos);
}

TEST(SessionHistoryVisualizerTest, GenerateHtmlContainsMessageContent) {
  SessionHistoryVisualizer visualizer;
  const std::string html = visualizer.GenerateHtml(CreateSampleSessionHistory());
  EXPECT_NE(html.find("You are an intelligent assistant."), std::string::npos);
  EXPECT_NE(html.find("Task: `Fix the bug`."), std::string::npos);
}

TEST(SessionHistoryVisualizerTest, GenerateHtmlContainsToolCalls) {
  SessionHistoryVisualizer visualizer;
  const std::string html = visualizer.GenerateHtml(CreateSampleSessionHistory());
  EXPECT_NE(html.find("call_123"), std::string::npos);
  EXPECT_NE(html.find("SearchAndReplace"), std::string::npos);
  EXPECT_NE(html.find("/workspace/a.cc"), std::string::npos);
}

TEST(SessionHistoryVisualizerTest, GenerateHtmlContainsSlashCommand) {
  SessionHistoryVisualizer visualizer;
  const std::string html = visualizer.GenerateHtml(CreateSampleSessionHistory());
  EXPECT_NE(html.find("/clear"), std::string::npos);
  EXPECT_NE(html.find("slash command"), std::string::npos);
}

TEST(SessionHistoryVisualizerTest, GenerateHtmlEscapesHtmlSpecialCharacters) {
  SessionHistoryVisualizer visualizer;
  const nlohmann::json session_history_json = nlohmann::json::parse(R"({
    "creationTimestamp": "20260811_120956",
    "agentTurns": [
      {
        "type": "message",
        "message": {
          "role": "user",
          "content": "<test-tag>alert('xss')</test-tag> & <b>bold</b>"
        }
      }
    ]
  })");
  const std::string html = visualizer.GenerateHtml(session_history_json);
  EXPECT_EQ(html.find("<test-tag>"), std::string::npos);
  EXPECT_NE(html.find("&lt;test-tag&gt;"), std::string::npos);
  EXPECT_NE(html.find("&amp;"), std::string::npos);
}

}  // namespace
}  // namespace jiaolong
