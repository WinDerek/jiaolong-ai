#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

// Expose the private members of Agent (e.g. ExecuteToolCalls, messages_) so
// that the unit test can drive the tool call dispatch directly.
#define private public
#include "agent/jiaolong_agent.h"
#undef private

#include "agent/session_history_visualizer/session_history_visualizer.h"

namespace jiaolong {
namespace {

std::shared_ptr<Agent> CreateTestAgent() {
  std::vector<AllowedBashCommand> allowed_bash_commands;
  std::vector<std::string> forbidden_to_write_file_list;
  return std::make_shared<Agent>(
      /*llm_api_base_url=*/ "http://localhost:1234/v1/chat/completions",
      /*llm_model=*/ "test-model",
      /*security_key=*/"test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ allowed_bash_commands,
      /*socks_proxy=*/ "test_socks_proxy",
      forbidden_to_write_file_list);
}

// Drives a single tool call through `Agent::ExecuteToolCalls` and returns the
// content of the resulting tool message. `tool_name` is the tool function
// name (e.g. "ReadWholeFile") and `arguments` is the parsed argument object
// that the LLM would have produced. This lets a test exercise the agent's
// path sandbox exactly the way a real (possibly malicious) LLM would.
std::string ExecuteAgentToolCall(Agent& agent,
    const std::string& tool_name,
    const nlohmann::json& arguments,
    const std::string& task_working_directory) {
  nlohmann::json tool_calls = nlohmann::json::array();
  tool_calls.push_back({
      {"id", "call_" + tool_name},
      {"type", "function"},
      {"function", {
          {"name", tool_name},
          {"arguments", arguments.dump()}
      }}
  });
  const size_t message_count_before = agent.messages_.size();
  agent.ExecuteToolCalls(tool_calls, task_working_directory);
  if (agent.messages_.size() <= message_count_before) {
    return "";
  }
  const nlohmann::json& tool_message = agent.messages_.back();
  if (!tool_message.contains("content") ||
      !tool_message["content"].is_string()) {
    return "";
  }
  return tool_message["content"].get<std::string>();
}

// Builds the canonical "invalid input" error message emitted by
// `StringFieldErrorMessage` when a required field is missing.
std::string MissingRequiredFieldMessage(const std::string& field) {
  return "invalid input, error_message: JSON is missing required field \"" +
      field + "\".";
}

// Builds the canonical "invalid input" error message emitted by
// `StringFieldErrorMessage` when a required string field has the wrong type.
std::string WrongTypedStringFieldMessage(const std::string& field) {
  return "invalid input, error_message: JSON field \"" + field +
      "\" must be a string.";
}

// Builds the canonical "invalid input" error message emitted by
// `IntFieldErrorMessage` when a required integer field has the wrong type.
std::string WrongTypedIntFieldMessage(const std::string& field) {
  return "invalid input, error_message: JSON field \"" + field +
      "\" must be an integer.";
}

// Drives a single, arbitrarily malformed raw tool call entry through
// `Agent::ExecuteToolCalls` and returns the resulting tool message JSON.
// Unlike `ExecuteAgentToolCall`, this gives the test full control over the raw
// tool call structure (e.g. a non-object entry, a missing `function` object, a
// `name`/`arguments` field of the wrong type, ...). Returns a null JSON value
// when no tool message was appended.
nlohmann::json ExecuteRawToolCall(Agent& agent,
    const nlohmann::json& tool_call,
    const std::string& task_working_directory) {
  nlohmann::json tool_calls = nlohmann::json::array();
  tool_calls.push_back(tool_call);
  const size_t message_count_before = agent.messages_.size();
  agent.ExecuteToolCalls(tool_calls, task_working_directory);
  if (agent.messages_.size() <= message_count_before) {
    return nlohmann::json();
  }
  return agent.messages_.back();
}

// Describes one (tool name, raw arguments, expected tool message content)
// malformed tool call case.
struct ToolArgumentCase {
  std::string tool_name;
  nlohmann::json arguments;
  std::string expected_content;
};

class JiaolongAgentTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static int test_id = 0;
    home_dir_ = std::filesystem::temp_directory_path() /
        ("jiaolong_agent_test_home_" + std::to_string(++test_id));
    std::filesystem::create_directories(home_dir_ / ".jiaolong");

    original_home_ = std::getenv("HOME") == nullptr ? "" : std::getenv("HOME");
    setenv("HOME", home_dir_.c_str(), 1);
  }

  void TearDown() override {
    if (original_home_.empty()) {
      unsetenv("HOME");
    } else {
      setenv("HOME", original_home_.c_str(), 1);
    }
    std::filesystem::remove_all(home_dir_);
  }

  std::filesystem::path home_dir_;
  std::string original_home_;
};

TEST_F(JiaolongAgentTest, ListDirectoryToolCallIsExecuted) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_working_dir_" + std::to_string(++test_id));
  std::filesystem::create_directories(working_dir / "sub");
  {
    std::ofstream file(working_dir / "a.txt");
    file << "hello\n";
  }

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  nlohmann::json tool_calls = nlohmann::json::array();
  tool_calls.push_back({
      {"id", "call_list_directory_1"},
      {"type", "function"},
      {"function", {
          {"name", "ListDirectory"},
          {"arguments", R"({"directoryPath": "/workspace"})"}
      }}
  });

  agent->ExecuteToolCalls(tool_calls, working_dir.string());

  // A tool message should have been appended for the ListDirectory call.
  ASSERT_FALSE(agent->messages_.empty());
  const nlohmann::json& tool_message = agent->messages_.back();
  EXPECT_EQ(tool_message["role"], "tool");
  EXPECT_EQ(tool_message["tool_call_id"], "call_list_directory_1");

  // The tool result should contain the immediate entries of the working
  // directory (virtual path `/workspace` maps to the working directory).
  const nlohmann::json result_json =
      nlohmann::json::parse(tool_message["content"].get<std::string>());
  ASSERT_TRUE(result_json.contains("entries"));
  ASSERT_EQ(result_json["entries"].size(), 2);
  EXPECT_EQ(result_json["entries"][0]["name"], "a.txt");
  EXPECT_EQ(result_json["entries"][0]["isDirectory"], false);
  EXPECT_EQ(result_json["entries"][1]["name"], "sub");
  EXPECT_EQ(result_json["entries"][1]["isDirectory"], true);

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ListDirectoryToolCallWithInvalidPath) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_working_dir_" + std::to_string(++test_id));
  std::filesystem::create_directories(working_dir);

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  nlohmann::json tool_calls = nlohmann::json::array();
  tool_calls.push_back({
      {"id", "call_list_directory_2"},
      {"type", "function"},
      {"function", {
          {"name", "ListDirectory"},
          {"arguments", R"({"directoryPath": "/workspace/does_not_exist"})"}
      }}
  });

  agent->ExecuteToolCalls(tool_calls, working_dir.string());

  ASSERT_FALSE(agent->messages_.empty());
  const nlohmann::json& tool_message = agent->messages_.back();
  EXPECT_EQ(tool_message["role"], "tool");
  EXPECT_EQ(tool_message["tool_call_id"], "call_list_directory_2");
  EXPECT_EQ(tool_message["content"],
      "invalid input, error_message: Invalid directory path");

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ReadFileToolCallIsExecuted) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_working_dir_" + std::to_string(++test_id));
  std::filesystem::create_directories(working_dir);
  {
    std::ofstream file(working_dir / "a.txt");
    file << "line 1\nline 2\nline 3\n";
  }

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  nlohmann::json tool_calls = nlohmann::json::array();
  tool_calls.push_back({
      {"id", "call_read_file_1"},
      {"type", "function"},
      {"function", {
          {"name", "ReadFile"},
          {"arguments", R"({"filePath": "/workspace/a.txt", "beginLine": 2, "endLine": 3})"}
      }}
  });

  agent->ExecuteToolCalls(tool_calls, working_dir.string());

  // A tool message should have been appended for the ReadFile call.
  ASSERT_FALSE(agent->messages_.empty());
  const nlohmann::json& tool_message = agent->messages_.back();
  EXPECT_EQ(tool_message["role"], "tool");
  EXPECT_EQ(tool_message["tool_call_id"], "call_read_file_1");

  // The tool result should contain only the requested line range.
  const nlohmann::json result_json =
      nlohmann::json::parse(tool_message["content"].get<std::string>());
  ASSERT_TRUE(result_json.contains("fileContent"));
  EXPECT_EQ(result_json["fileContent"], "line 2\nline 3\n");

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ReadFileToolCallWithInvalidPath) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_working_dir_" + std::to_string(++test_id));
  std::filesystem::create_directories(working_dir);

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  nlohmann::json tool_calls = nlohmann::json::array();
  tool_calls.push_back({
      {"id", "call_read_file_2"},
      {"type", "function"},
      {"function", {
          {"name", "ReadFile"},
          {"arguments", R"({"filePath": "/workspace/does_not_exist.txt", "beginLine": 1, "endLine": 1})"}
      }}
  });

  agent->ExecuteToolCalls(tool_calls, working_dir.string());

  ASSERT_FALSE(agent->messages_.empty());
  const nlohmann::json& tool_message = agent->messages_.back();
  EXPECT_EQ(tool_message["role"], "tool");
  EXPECT_EQ(tool_message["tool_call_id"], "call_read_file_2");
  EXPECT_EQ(tool_message["content"],
      "invalid input, error_message: Invalid file path");

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ParseLlmResponseFailsFastWhenFinishReasonMissing) {
  std::shared_ptr<Agent> agent = CreateTestAgent();

  std::string finish_reason;
  std::string reasoning_content;
  std::string content;
  int total_tokens = 0;
  auto parse = [&](const nlohmann::json& response_json) {
    return agent->ParseLlmResponse(response_json, finish_reason,
        reasoning_content, content, total_tokens);
  };

  // Response without a `choices` field at all.
  EXPECT_FALSE(parse(nlohmann::json::parse(
      R"({"usage": {"total_tokens": 10}})") ));

  // Response with an empty `choices` array.
  EXPECT_FALSE(parse(nlohmann::json::parse(R"({"choices": []})")));

  // Response whose first choice does not carry `finish_reason`.
  EXPECT_FALSE(parse(nlohmann::json::parse(R"({
    "choices": [{"message": {"content": "hello"}}]
  })")));
}

TEST_F(JiaolongAgentTest, ParseLlmResponseFailsFastWhenFinishReasonNullOrWrongType) {
  std::shared_ptr<Agent> agent = CreateTestAgent();

  std::string finish_reason;
  std::string reasoning_content;
  std::string content;
  int total_tokens = 0;
  auto parse = [&](const nlohmann::json& response_json) {
    return agent->ParseLlmResponse(response_json, finish_reason,
        reasoning_content, content, total_tokens);
  };

  // `finish_reason` is null.
  EXPECT_FALSE(parse(nlohmann::json::parse(R"({
    "choices": [{
      "message": {"content": "hello"},
      "finish_reason": null
    }]
  })") ));

  // `finish_reason` has a non-string type.
  EXPECT_FALSE(parse(nlohmann::json::parse(R"({
    "choices": [{
      "message": {"content": "hello"},
      "finish_reason": 42
    }]
  })") ));
}

TEST_F(JiaolongAgentTest, ParseLlmResponseExtractsAllFields) {
  std::shared_ptr<Agent> agent = CreateTestAgent();

  nlohmann::json response_json = nlohmann::json::parse(R"({
    "choices": [{
      "message": {
        "reasoning_content": "thinking",
        "content": "answer"
      },
      "finish_reason": "stop"
    }],
    "usage": {"total_tokens": 42}
  })");

  std::string finish_reason;
  std::string reasoning_content;
  std::string content;
  int total_tokens = 0;
  agent->ParseLlmResponse(response_json, finish_reason, reasoning_content,
      content, total_tokens);

  EXPECT_EQ(finish_reason, "stop");
  EXPECT_EQ(reasoning_content, "thinking");
  EXPECT_EQ(content, "answer");
  EXPECT_EQ(total_tokens, 42);
}

TEST_F(JiaolongAgentTest, ParseLlmResponseDefaultsOptionalFieldsWhenMissingOrMalformed) {
  std::shared_ptr<Agent> agent = CreateTestAgent();

  // Only `finish_reason` is guaranteed; optional fields are missing, null, or
  // of a different type.
  nlohmann::json response_json = nlohmann::json::parse(R"({
    "choices": [{
      "message": {
        "reasoning_content": null,
        "content": 123
      },
      "finish_reason": "length"
    }]
  })");

  std::string finish_reason;
  std::string reasoning_content;
  std::string content;
  int total_tokens = 0;
  agent->ParseLlmResponse(response_json, finish_reason, reasoning_content,
      content, total_tokens);

  EXPECT_EQ(finish_reason, "length");
  EXPECT_EQ(reasoning_content, "");
  EXPECT_EQ(content, "");
  EXPECT_EQ(total_tokens, 0);
}

TEST_F(JiaolongAgentTest, PersistedSessionHistoryGeneratesHtmlWithAgentTurns) {
  // Starting the agent (even without doing any work) must persist an initial
  // session history that contains agent turns. The session history visualizer
  // must render agent turn elements from that persisted file. No LLM API call
  // is made by merely constructing the agent.
  std::shared_ptr<Agent> agent = CreateTestAgent();

  // Locate the persisted session history file under the test HOME directory.
  const std::filesystem::path sessions_dir =
      home_dir_ / ".jiaolong" / "sessions";
  ASSERT_TRUE(std::filesystem::is_directory(sessions_dir));

  std::filesystem::path session_file_path;
  for (const auto& entry : std::filesystem::directory_iterator(sessions_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      session_file_path = entry.path();
      break;
    }
  }
  ASSERT_FALSE(session_file_path.empty());

  std::ifstream session_file(session_file_path);
  ASSERT_TRUE(session_file.is_open());
  nlohmann::json session_history_json;
  session_file >> session_history_json;

  // The persisted session history must use the `agentTurns` key, which is what
  // the visualizer reads.
  ASSERT_TRUE(session_history_json.contains("agentTurns"));
  ASSERT_TRUE(session_history_json["agentTurns"].is_array());
  ASSERT_FALSE(session_history_json["agentTurns"].empty());

  // The generated HTML must contain agent turn elements, including the initial
  // system message recorded when the agent started.
  SessionHistoryVisualizer visualizer;
  const std::string html = visualizer.GenerateHtml(session_history_json);
  EXPECT_NE(html.find("<div class=\"turn\">"), std::string::npos);
  EXPECT_NE(html.find("You are an intelligent assistant."), std::string::npos);
}

// A valid chat completion body that passes Agent::ParseLlmResponse.
constexpr char kChatCompletionBody[] = R"({
  "choices": [{
    "message": {
      "role": "assistant",
      "content": "ok",
      "tool_calls": []
    },
    "finish_reason": "stop"
  }],
  "usage": {"total_tokens": 10}
})";

// A chat completion body whose `finish_reason` is "length", modelling an LLM
// response that did not stop naturally (e.g. it hit the output token limit).
constexpr char kChatCompletionBodyLengthFinish[] = R"({
  "choices": [{
    "message": {
      "role": "assistant",
      "content": "ok",
      "tool_calls": []
    },
    "finish_reason": "length"
  }],
  "usage": {"total_tokens": 1000}
})";

// Serves a fake LLM chat completion endpoint on an ephemeral loopback port.
// Each request consumes the next status code from `status_codes`; the last
// status code is repeated once the list is exhausted. A status 200 response
// carries a valid chat completion body so the agent can parse it.
class FakeLlmApiServer {
 public:
  explicit FakeLlmApiServer(std::vector<int> status_codes,
      std::string success_body = kChatCompletionBody)
      : status_codes_(std::move(status_codes)),
        success_body_(std::move(success_body)) {}

  ~FakeLlmApiServer() {
    Stop();
  }

  bool Start() {
    server_.Post("/v1/chat/completions", [this](const httplib::Request&,
                                                httplib::Response& res) {
      const int request_index = request_count_.fetch_add(1);
      const size_t index =
          std::min<size_t>(request_index, status_codes_.size() - 1);
      const int status = status_codes_[index];
      res.status = status;
      if (status == 200) {
        res.set_content(success_body_, "application/json");
      } else {
        res.set_content("{\"error\": {\"message\": \"fake error\"}}",
                        "application/json");
      }
    });

    port_ = server_.bind_to_any_port("127.0.0.1");
    if (port_ <= 0) {
      return false;
    }

    server_thread_ = std::thread([this] { server_.listen_after_bind(); });

    // Wait until the server is actually accepting connections so the first
    // request made by the agent does not race with server startup.
    for (int i = 0; i < 100 && !server_.is_running(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return server_.is_running();
  }

  void Stop() {
    if (stopped_) {
      return;
    }
    stopped_ = true;
    server_.stop();
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  int port() const { return port_; }
  int request_count() const { return request_count_.load(); }

 private:
  httplib::Server server_;
  std::vector<int> status_codes_;
  std::string success_body_;
  std::atomic<int> request_count_{0};
  std::thread server_thread_;
  int port_ = -1;
  bool stopped_ = false;
};

std::string FakeLlmApiBaseUrl(const FakeLlmApiServer& server) {
  return "http://127.0.0.1:" + std::to_string(server.port()) +
      "/v1/chat/completions";
}

TEST_F(JiaolongAgentTest, SendMessagesFailsFastWhenRetryTimesIsNotSet) {
  FakeLlmApiServer server({500});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});

  EXPECT_FALSE(agent.SendMessages().has_value());
  EXPECT_EQ(server.request_count(), 1);
}

TEST_F(JiaolongAgentTest, SendMessagesFailsFastWhenRetryTimesIsZero) {
  FakeLlmApiServer server({429, 200});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});

  EXPECT_FALSE(agent.SendMessages().has_value());
  EXPECT_EQ(server.request_count(), 1);
}

TEST_F(JiaolongAgentTest, SendMessagesRetriesNonOkStatusUntilSuccess) {
  FakeLlmApiServer server({500, 503, 200});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 2,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});

  const std::optional<nlohmann::json> response = agent.SendMessages();
  ASSERT_TRUE(response.has_value());
  const nlohmann::json& response_json = *response;
  EXPECT_EQ(server.request_count(), 3);
  EXPECT_EQ(response_json["choices"][0]["message"]["content"], "ok");
  EXPECT_EQ(response_json["usage"]["total_tokens"], 10);
  EXPECT_TRUE(agent.llm_stopped_);
}

TEST_F(JiaolongAgentTest, SendMessagesSucceedsOnLastRetry) {
  FakeLlmApiServer server({503, 503, 200});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 2,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});

  const std::optional<nlohmann::json> response = agent.SendMessages();
  ASSERT_TRUE(response.has_value());
  const nlohmann::json& response_json = *response;
  EXPECT_EQ(server.request_count(), 3);
  EXPECT_EQ(response_json["choices"][0]["message"]["content"], "ok");
}

TEST_F(JiaolongAgentTest, SendMessagesReturnsNulloptAfterExhaustingRetries) {
  FakeLlmApiServer server({500});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 3,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});

  EXPECT_FALSE(agent.SendMessages().has_value());
  // The initial request plus the 3 retries.
  EXPECT_EQ(server.request_count(), 4);
}

TEST_F(JiaolongAgentTest, WorkDoesNotSendLlmApiCallWhenWorkingDirectoryDoesNotExist) {
  FakeLlmApiServer server({200});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});
  agent.SetToolPermission(ToolPermission::kAllowAll);

  static int test_id = 0;
  const std::filesystem::path non_existing_working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_non_existing_working_dir_" + std::to_string(++test_id));
  // Make sure the working directory really does not exist.
  std::filesystem::remove_all(non_existing_working_dir);

  Task task("test_id", "test title", "test description", non_existing_working_dir.string(),
      /*token_limit=*/1000);
  agent.Work(task);

  // The agent must fail fast and never call the LLM API.
  EXPECT_EQ(server.request_count(), 0);

  std::filesystem::remove_all(non_existing_working_dir);
}

TEST_F(JiaolongAgentTest, WorkAllowsSingleTrailingSlashInWorkingDirectory) {
  FakeLlmApiServer server({200});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});
  agent.SetToolPermission(ToolPermission::kAllowAll);

  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_single_trailing_slash_working_dir_" + std::to_string(++test_id));
  std::filesystem::create_directories(working_dir);

  // A single trailing slash is allowed: it should be stripped and the agent
  // should continue as if the working directory had no trailing slash.
  Task task("single_trailing_slash_task", "title", "description",
      working_dir.string() + "/", /*token_limit=*/1000);
  // The fake LLM server returns finish_reason "stop", so Work succeeds and
  // calls the LLM exactly once.
  EXPECT_TRUE(agent.Work(task));
  EXPECT_EQ(server.request_count(), 1);

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, WorkDoesNotSendLlmApiCallWhenWorkingDirectoryEndsWithMultipleSlashes) {
  FakeLlmApiServer server({200});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});
  agent.SetToolPermission(ToolPermission::kAllowAll);

  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_multiple_trailing_slash_working_dir_" + std::to_string(++test_id));
  std::filesystem::create_directories(working_dir);

  // Multiple trailing slashes are illegal: the agent must fail fast and never
  // call the LLM API.
  Task task("multiple_trailing_slash_task", "title", "description",
      working_dir.string() + "//", /*token_limit=*/1000);
  EXPECT_FALSE(agent.Work(task));

  EXPECT_EQ(server.request_count(), 0);

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, WorkHandlesTaskTitleAndDescriptionWithSpecialCharacters) {
  FakeLlmApiServer server({200});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});
  agent.SetToolPermission(ToolPermission::kAllowAll);

  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_special_char_working_dir_" + std::to_string(++test_id));
  std::filesystem::create_directories(working_dir);

  const std::string special_title = "Fix \"quoted\" {braced} } title";
  const std::string special_description =
      "Description with \"double quotes\", {open brace}, }close brace}.";
  Task task("special_char_task", special_title, special_description,
      working_dir.string(), /*token_limit=*/1000);

  // Work must not crash (e.g. from a malformed JSON request body or a
  // std::format error) when the title/description contain `"`, `{` and `}`.
  agent.Work(task);

  // The fake LLM server returns a chat completion with finish_reason "stop",
  // so Work should have sent exactly one request and stopped.
  EXPECT_EQ(server.request_count(), 1);

  // The user message recorded in the conversation must contain the special
  // characters exactly as provided by the task.
  bool found_user_message = false;
  for (const nlohmann::json& message : agent.messages_) {
    if (message.contains("role") && message["role"] == "user") {
      found_user_message = true;
      const std::string content = message["content"].get<std::string>();
      EXPECT_NE(content.find(special_title), std::string::npos);
      EXPECT_NE(content.find(special_description), std::string::npos);
    }
  }
  EXPECT_TRUE(found_user_message);

  // The request body that was serialized for the LLM API must remain valid
  // JSON (double quotes in the title/description must be escaped) and must
  // round-trip the original text.
  const std::string request_body = agent.request_json_.dump();
  const nlohmann::json parsed_request =
      nlohmann::json::parse(request_body, nullptr, false);
  ASSERT_FALSE(parsed_request.is_discarded());
  const std::string sent_content =
      parsed_request["messages"][1]["content"].get<std::string>();
  EXPECT_NE(sent_content.find(special_title), std::string::npos);
  EXPECT_NE(sent_content.find(special_description), std::string::npos);

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, WorkReturnsTrueWhenLlmStopsNaturally) {
  FakeLlmApiServer server({200});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});
  agent.SetToolPermission(ToolPermission::kAllowAll);

  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_llm_stopped_working_dir_" + std::to_string(++test_id));
  std::filesystem::create_directories(working_dir);

  Task task("llm_stopped_task", "title", "description", working_dir.string(),
      /*token_limit=*/1000);
  // The fake LLM server returns finish_reason "stop", so the last response
  // indicates the LLM stopped naturally.
  EXPECT_TRUE(agent.Work(task));
  EXPECT_EQ(server.request_count(), 1);

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, PersistedSessionHistoryContainsTaskIdAfterWork) {
  FakeLlmApiServer server({200});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});
  agent.SetToolPermission(ToolPermission::kAllowAll);

  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_task_id_working_dir_" + std::to_string(++test_id));
  std::filesystem::create_directories(working_dir);

  const std::string task_id = "associated_task_uuid";
  Task task(task_id, "title", "description", working_dir.string(),
      /*token_limit=*/1000);
  ASSERT_TRUE(agent.Work(task));

  // The persisted session history JSON must carry the task id so the session
  // file is associated with the task that produced it.
  const std::filesystem::path sessions_dir =
      home_dir_ / ".jiaolong" / "sessions";
  ASSERT_TRUE(std::filesystem::is_directory(sessions_dir));

  std::filesystem::path session_file_path;
  for (const auto& entry : std::filesystem::directory_iterator(sessions_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      session_file_path = entry.path();
      break;
    }
  }
  ASSERT_FALSE(session_file_path.empty());

  std::ifstream session_file(session_file_path);
  ASSERT_TRUE(session_file.is_open());
  nlohmann::json session_history_json;
  session_file >> session_history_json;
  ASSERT_TRUE(session_history_json.contains("taskId"));
  EXPECT_EQ(session_history_json["taskId"], task_id);

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, WorkReturnsFalseWhenLlmApiFails) {
  FakeLlmApiServer server({500});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});
  agent.SetToolPermission(ToolPermission::kAllowAll);

  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_llm_failed_working_dir_" + std::to_string(++test_id));
  std::filesystem::create_directories(working_dir);

  Task task("llm_failed_task", "title", "description", working_dir.string(),
      /*token_limit=*/1000);
  // The LLM API never returns a valid response, so the agent never receives
  // a response where the LLM stopped.
  EXPECT_FALSE(agent.Work(task));

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, WorkReturnsFalseWhenTokenLimitReachedBeforeLlmStops) {
  FakeLlmApiServer server({200}, kChatCompletionBodyLengthFinish);
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});
  agent.SetToolPermission(ToolPermission::kAllowAll);

  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_token_limit_working_dir_" + std::to_string(++test_id));
  std::filesystem::create_directories(working_dir);

  // The fake LLM response reports 1000 tokens and finish_reason "length".
  // With a token limit of 10 the agent stops because the limit is reached,
  // not because the LLM stopped naturally.
  Task task("token_limit_task", "title", "description", working_dir.string(),
      /*token_limit=*/10);
  EXPECT_FALSE(agent.Work(task));

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, WorkWithResumeLoadsPreviousSessionHistory) {
  FakeLlmApiServer server({200});
  ASSERT_TRUE(server.Start());

  // First agent works on the task and persists the session history (carrying
  // the task id) into the test HOME's sessions folder.
  Agent agent1(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});
  agent1.SetToolPermission(ToolPermission::kAllowAll);

  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_resume_working_dir_" + std::to_string(++test_id));
  std::filesystem::create_directories(working_dir);

  const std::string task_id = "resume_task_id";
  Task task(task_id, "title", "description", working_dir.string(),
      /*token_limit=*/1000);
  ASSERT_TRUE(agent1.Work(task));
  EXPECT_EQ(server.request_count(), 1);
  const std::string original_session_id = agent1.session_id_;

  // A second agent resumes the same task: it must load the previous session
  // history (including the original task user message) and continue working
  // without appending a fresh task user message.
  Agent agent2(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});
  agent2.SetToolPermission(ToolPermission::kAllowAll);

  ASSERT_TRUE(agent2.Work(task, /*resume=*/true));
  // One LLM request for the first round + one for the resumed round.
  EXPECT_EQ(server.request_count(), 2);

  // The resumed session reuses the original session id and contains exactly
  // one user message (the original one), i.e. no fresh task message appended.
  EXPECT_EQ(agent2.session_id_, original_session_id);
  int user_message_count = 0;
  for (const nlohmann::json& message : agent2.messages_) {
    if (message.contains("role") && message["role"] == "user") {
      ++user_message_count;
    }
  }
  EXPECT_EQ(user_message_count, 1);

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, WorkWithResumeStartsFreshWhenNoSessionHistoryExists) {
  FakeLlmApiServer server({200});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});
  agent.SetToolPermission(ToolPermission::kAllowAll);

  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_resume_fallback_working_dir_" + std::to_string(++test_id));
  std::filesystem::create_directories(working_dir);

  // No session history exists for this task, so resuming must fall back to a
  // fresh round: a task user message is appended and the LLM is still called.
  Task task("resume_fallback_task_id", "title", "description",
      working_dir.string(), /*token_limit=*/1000);
  ASSERT_TRUE(agent.Work(task, /*resume=*/true));
  EXPECT_EQ(server.request_count(), 1);

  int user_message_count = 0;
  for (const nlohmann::json& message : agent.messages_) {
    if (message.contains("role") && message["role"] == "user") {
      ++user_message_count;
    }
  }
  EXPECT_EQ(user_message_count, 1);

  std::filesystem::remove_all(working_dir);
}

// The agent only exposes the virtual `/workspace` prefix to the LLM and maps
// it onto the task working directory; every other path (a host absolute path
// or a relative path) is rejected. Read-only tools therefore must never be
// able to read a file that lives outside of the task working directory.
TEST_F(JiaolongAgentTest, ReadToolsRejectPathsOutsideTaskWorkingDirectory) {
  static int test_id = 0;
  const int id = ++test_id;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_read_sandbox_working_dir_" + std::to_string(id));
  const std::filesystem::path outside_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_read_sandbox_outside_dir_" + std::to_string(id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(outside_dir);
  std::filesystem::create_directories(working_dir);
  std::filesystem::create_directories(outside_dir);
  const std::filesystem::path outside_file = outside_dir / "secret.txt";
  {
    std::ofstream file(outside_file);
    file << "TOP SECRET CONTENT\n";
  }

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  const std::string outside_file_path = outside_file.string();
  const std::string outside_dir_path = outside_dir.string();

  // ReadWholeFile must refuse to read the outside file even when handed its
  // real host path.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "ReadWholeFile",
      nlohmann::json({{"filePath", outside_file_path}}), working_dir.string()),
      "invalid input, error_message: invalid file path.");

  // ReadFile must refuse to read the outside file as well.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "ReadFile",
      nlohmann::json({{"filePath", outside_file_path},
          {"beginLine", 1}, {"endLine", 1}}), working_dir.string()),
      "invalid input, error_message: invalid file path.");

  // ListDirectory must refuse to enumerate the outside directory.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "ListDirectory",
      nlohmann::json({{"directoryPath", outside_dir_path}}),
      working_dir.string()),
      "invalid input, error_message: invalid directory path.");

  // FindFiles must refuse to search the outside directory.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "FindFiles",
      nlohmann::json({{"rootPath", outside_dir_path}, {"query", "secret"}}),
      working_dir.string()),
      "invalid input, error_message: invalid root path.");

  // FindStrings must refuse to search the outside directory.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "FindStrings",
      nlohmann::json({{"rootPath", outside_dir_path}, {"query", "SECRET"}}),
      working_dir.string()),
      "invalid input, error_message: invalid root path.");

  // No tool result may leak the outside file's content.
  for (const nlohmann::json& message : agent->messages_) {
    EXPECT_EQ(message["content"].get<std::string>().find("TOP SECRET"),
        std::string::npos);
  }

  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(outside_dir);
}

// A hacking test case to use `/workspace/../README.md` as the file path for tools.
TEST_F(JiaolongAgentTest, ReadToolsRejectPathsOutsideTaskWorkingDirectoryEvenWithHack) {
  static int test_id = 0;
  const int id = ++test_id;
  const std::filesystem::path outside_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_read_sandbox_outside_dir_" + std::to_string(id));
  const std::filesystem::path working_dir =
      outside_dir / "task_working_directory";
  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(outside_dir);
  std::filesystem::create_directories(outside_dir);
  std::filesystem::create_directories(working_dir);
  const std::filesystem::path outside_file = outside_dir / "secret.txt";
  {
    std::ofstream file(outside_file);
    file << "TOP SECRET CONTENT\n";
  }

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  const std::string outside_file_path = "/workspace/../secret.txt";
  const std::string outside_dir_path = "/workspace/../";

  // ReadWholeFile must refuse to read the outside file even when handed its
  // real host path.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "ReadWholeFile",
      nlohmann::json({{"filePath", outside_file_path}}), working_dir.string()),
      "invalid input, error_message: invalid file path.");

  // ReadFile must refuse to read the outside file as well.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "ReadFile",
      nlohmann::json({{"filePath", outside_file_path},
          {"beginLine", 1}, {"endLine", 1}}), working_dir.string()),
      "invalid input, error_message: invalid file path.");

  // ListDirectory must refuse to enumerate the outside directory.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "ListDirectory",
      nlohmann::json({{"directoryPath", outside_dir_path}}),
      working_dir.string()),
      "invalid input, error_message: invalid directory path.");

  // FindFiles must refuse to search the outside directory.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "FindFiles",
      nlohmann::json({{"rootPath", outside_dir_path}, {"query", "secret"}}),
      working_dir.string()),
      "invalid input, error_message: invalid root path.");

  // FindStrings must refuse to search the outside directory.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "FindStrings",
      nlohmann::json({{"rootPath", outside_dir_path}, {"query", "SECRET"}}),
      working_dir.string()),
      "invalid input, error_message: invalid root path.");

  // No tool result may leak the outside file's content.
  for (const nlohmann::json& message : agent->messages_) {
    EXPECT_EQ(message["content"].get<std::string>().find("TOP SECRET"),
        std::string::npos);
  }

  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(outside_dir);
}

// Write-capable tools must refuse to create, modify, move, copy or delete any
// path outside of the task working directory, and they must leave the outside
// file system completely untouched.
TEST_F(JiaolongAgentTest, WriteToolsRejectPathsOutsideTaskWorkingDirectory) {
  static int test_id = 0;
  const int id = ++test_id;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_write_sandbox_working_dir_" + std::to_string(id));
  const std::filesystem::path outside_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_write_sandbox_outside_dir_" + std::to_string(id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(outside_dir);
  std::filesystem::create_directories(working_dir);
  std::filesystem::create_directories(outside_dir);
  const std::filesystem::path victim_file = outside_dir / "victim.txt";
  {
    std::ofstream file(victim_file);
    file << "ORIGINAL CONTENT\n";
  }

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  const std::string victim_path = victim_file.string();

  // WriteContentToFile must not overwrite the outside file.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "WriteContentToFile",
      nlohmann::json({{"filePath", victim_path}, {"content", "HACKED"}}),
      working_dir.string()),
      "invalid input, error_message: invalid file path.");

  // SearchAndReplace must not modify the outside file.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "SearchAndReplace",
      nlohmann::json({{"filePath", victim_path}, {"oldStr", "ORIGINAL"},
          {"newStr", "HACKED"}}), working_dir.string()),
      "invalid input, error_message: invalid file path.");

  // CreateNewFile must not create a file outside the working directory.
  const std::filesystem::path created_file = outside_dir / "created.txt";
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "CreateNewFile",
      nlohmann::json({{"filePath", created_file.string()},
          {"initialContent", "x"}}), working_dir.string()),
      "invalid input, error_message: invalid file path.");

  // CreateNewDirectory must not create a directory outside the working
  // directory. (The production message intentionally keeps its existing
  // spelling.)
  const std::filesystem::path created_dir = outside_dir / "created_dir";
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "CreateNewDirectory",
      nlohmann::json({{"directoryPath", created_dir.string()}}),
      working_dir.string()),
      "invalid input, error_message: invalie directory path.");

  // Delete must not delete the outside file.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "Delete",
      nlohmann::json({{"path", victim_path}}), working_dir.string()),
      "invalid input, error_message: invalid path.");

  // Move must not move the outside file anywhere.
  const std::filesystem::path moved_file = outside_dir / "moved.txt";
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "Move",
      nlohmann::json({{"sourcePath", victim_path},
          {"destinationPath", moved_file.string()}}), working_dir.string()),
      "invalid input, error_message: invalid path.");

  // Copy must not copy the outside file anywhere.
  const std::filesystem::path copied_file = outside_dir / "copied.txt";
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "Copy",
      nlohmann::json({{"sourcePath", victim_path},
          {"destinationPath", copied_file.string()}}), working_dir.string()),
      "invalid input, error_message: invalid path.");

  // Nothing outside the working directory may have changed.
  EXPECT_TRUE(std::filesystem::exists(victim_file));
  EXPECT_FALSE(std::filesystem::exists(created_file));
  EXPECT_FALSE(std::filesystem::exists(created_dir));
  EXPECT_FALSE(std::filesystem::exists(moved_file));
  EXPECT_FALSE(std::filesystem::exists(copied_file));
  {
    std::ifstream victim_stream(victim_file);
    std::string victim_content;
    std::getline(victim_stream, victim_content);
    EXPECT_EQ(victim_content, "ORIGINAL CONTENT");
  }

  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(outside_dir);
}

// Paths that do not start with the virtual `/workspace/` prefix are rejected,
// whether they are absolute host paths (e.g. `/etc/passwd`) or relative paths
// that would otherwise escape the working directory.
TEST_F(JiaolongAgentTest, ToolsRejectPathsThatDoNotMapToTheWorkingDirectory) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_path_prefix_working_dir_" +
       std::to_string(++test_id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::create_directories(working_dir);

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  const std::vector<std::string> outside_paths = {
      "/etc/passwd",
      "/etc",
      "/tmp",
      "secret.txt",
      "../secret.txt",
      "workspace/secret.txt",
      "/workspace_evil/secret.txt",
  };

  // All of these must be rejected by a read tool...
  for (const std::string& outside_path : outside_paths) {
    EXPECT_EQ(ExecuteAgentToolCall(*agent, "ReadWholeFile",
        nlohmann::json({{"filePath", outside_path}}), working_dir.string()),
        "invalid input, error_message: invalid file path.")
        << "path: " << outside_path;
  }

  // ...and by a write tool, without creating anything in the working
  // directory.
  for (const std::string& outside_path : outside_paths) {
    EXPECT_EQ(ExecuteAgentToolCall(*agent, "WriteContentToFile",
        nlohmann::json({{"filePath", outside_path}, {"content", "HACKED"}}),
        working_dir.string()),
        "invalid input, error_message: invalid file path.")
        << "path: " << outside_path;
  }

  // Listing the `/etc` host directory must be refused too.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "ListDirectory",
      nlohmann::json({{"directoryPath", "/etc"}}), working_dir.string()),
      "invalid input, error_message: invalid directory path.");

  // The working directory must remain empty: no rejected path may have
  // created a file inside it.
  EXPECT_TRUE(std::filesystem::is_empty(working_dir));

  std::filesystem::remove_all(working_dir);
}

// Sanity check for the other side of the boundary: paths that do start with
// the virtual `/workspace/` prefix are mapped onto the task working directory
// and work as expected. This proves the sandbox allows legitimate access while
// still rejecting everything outside it.
TEST_F(JiaolongAgentTest, ToolsAllowPathsInsideTaskWorkingDirectory) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_inside_sandbox_working_dir_" +
       std::to_string(++test_id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::create_directories(working_dir);
  {
    std::ofstream file(working_dir / "inside.txt");
    file << "inside content\n";
  }

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  // Reading through the virtual `/workspace` prefix reads the file in the
  // working directory.
  const std::string read_result = ExecuteAgentToolCall(*agent, "ReadWholeFile",
      nlohmann::json({{"filePath", "/workspace/inside.txt"}}),
      working_dir.string());
  const nlohmann::json read_json =
      nlohmann::json::parse(read_result, nullptr, false);
  ASSERT_FALSE(read_json.is_discarded());
  ASSERT_TRUE(read_json.contains("fileContent"));
  EXPECT_EQ(read_json["fileContent"], "inside content\n");

  // Writing through the virtual `/workspace` prefix writes inside the working
  // directory.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "WriteContentToFile",
      nlohmann::json({{"filePath", "/workspace/created.txt"},
          {"content", "new content"}}), working_dir.string()),
      "success");
  EXPECT_TRUE(std::filesystem::exists(working_dir / "created.txt"));

  // Deleting through the virtual `/workspace` prefix deletes inside the
  // working directory.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "Delete",
      nlohmann::json({{"path", "/workspace/created.txt"}}),
      working_dir.string()),
      "success");
  EXPECT_FALSE(std::filesystem::exists(working_dir / "created.txt"));

  std::filesystem::remove_all(working_dir);
}

// ---------------------------------------------------------------------------
// Malformed LLM tool calls.
//
// An LLM can return tool calls that are structurally malformed: a non-object
// entry, a missing `function` object, a `name`/`arguments` field of the wrong
// type, an `arguments` payload that is not valid JSON, or a tool call whose
// arguments are missing / miss-typed. The agent must never crash on any of
// these: it must report the problem back to the LLM as a `tool` message so the
// model can recover on its next turn.
// ---------------------------------------------------------------------------

TEST_F(JiaolongAgentTest, ToolCallWithoutFunctionObjectIsReportedAsMalformed) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_malformed_tool_call_working_dir_" +
       std::to_string(++test_id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::create_directories(working_dir);

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  // The tool call entry is not even a JSON object.
  const nlohmann::json string_entry = ExecuteRawToolCall(
      *agent, nlohmann::json("not-an-object"), working_dir.string());
  EXPECT_EQ(string_entry["role"], "tool");
  EXPECT_EQ(string_entry["tool_call_id"], "");
  EXPECT_EQ(string_entry["content"],
      "invalid input, error_message: malformed tool call.");

  // The tool call object does not carry a `function` field.
  const nlohmann::json missing_function = ExecuteRawToolCall(*agent,
      nlohmann::json({{"id", "call_missing_function"}}),
      working_dir.string());
  EXPECT_EQ(missing_function["role"], "tool");
  EXPECT_EQ(missing_function["tool_call_id"], "call_missing_function");
  EXPECT_EQ(missing_function["content"],
      "invalid input, error_message: malformed tool call.");

  // The `function` field is present but is not an object.
  const nlohmann::json function_not_object = ExecuteRawToolCall(*agent,
      nlohmann::json({{"id", "call_function_not_object"},
          {"function", "not-an-object"}}),
      working_dir.string());
  EXPECT_EQ(function_not_object["role"], "tool");
  EXPECT_EQ(function_not_object["tool_call_id"], "call_function_not_object");
  EXPECT_EQ(function_not_object["content"],
      "invalid input, error_message: malformed tool call.");

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ToolCallWithMissingFunctionNameIsRejected) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_missing_name_working_dir_" +
       std::to_string(++test_id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::create_directories(working_dir);

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  const nlohmann::json message = ExecuteRawToolCall(*agent,
      nlohmann::json({{"id", "call_missing_name"},
          {"function", {{"arguments", "{}"}}}}),
      working_dir.string());
  EXPECT_EQ(message["role"], "tool");
  EXPECT_EQ(message["tool_call_id"], "call_missing_name");
  EXPECT_EQ(message["content"], MissingRequiredFieldMessage("name"));

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ToolCallWithNonStringFunctionNameIsRejected) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_numeric_name_working_dir_" +
       std::to_string(++test_id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::create_directories(working_dir);

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  const nlohmann::json message = ExecuteRawToolCall(*agent,
      nlohmann::json({{"id", "call_numeric_name"},
          {"function", {{"name", 123}, {"arguments", "{}"}}}}),
      working_dir.string());
  EXPECT_EQ(message["role"], "tool");
  EXPECT_EQ(message["tool_call_id"], "call_numeric_name");
  EXPECT_EQ(message["content"], WrongTypedStringFieldMessage("name"));

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ToolCallWithMissingArgumentsIsRejected) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_missing_arguments_working_dir_" +
       std::to_string(++test_id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::create_directories(working_dir);

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  const nlohmann::json message = ExecuteRawToolCall(*agent,
      nlohmann::json({{"id", "call_missing_arguments"},
          {"function", {{"name", "ListDirectory"}}}}),
      working_dir.string());
  EXPECT_EQ(message["role"], "tool");
  EXPECT_EQ(message["tool_call_id"], "call_missing_arguments");
  EXPECT_EQ(message["content"], MissingRequiredFieldMessage("arguments"));

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ToolCallWithNonStringArgumentsIsRejected) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_object_arguments_working_dir_" +
       std::to_string(++test_id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::create_directories(working_dir);

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  // `arguments` is an object rather than the string the API contract requires.
  const nlohmann::json message = ExecuteRawToolCall(*agent,
      nlohmann::json({{"id", "call_object_arguments"},
          {"function", {{"name", "ListDirectory"},
              {"arguments", nlohmann::json::object()}}}}),
      working_dir.string());
  EXPECT_EQ(message["role"], "tool");
  EXPECT_EQ(message["tool_call_id"], "call_object_arguments");
  EXPECT_EQ(message["content"], WrongTypedStringFieldMessage("arguments"));

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ToolCallWithUnparseableArgumentsIsRejected) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_bad_json_working_dir_" + std::to_string(++test_id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::create_directories(working_dir);

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  // `arguments` is a string, but its contents are not valid JSON.
  const nlohmann::json message = ExecuteRawToolCall(*agent,
      nlohmann::json({{"id", "call_bad_json"},
          {"function", {{"name", "ListDirectory"},
              {"arguments", "{not valid json"}}}}),
      working_dir.string());
  EXPECT_EQ(message["role"], "tool");
  EXPECT_EQ(message["tool_call_id"], "call_bad_json");
  EXPECT_EQ(message["content"],
      "invalid input, error_message: failed to parse tool call arguments.");

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ToolCallWithNonObjectArgumentsIsRejected) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_array_arguments_working_dir_" +
       std::to_string(++test_id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::create_directories(working_dir);

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  // `arguments` is valid JSON but not an object, so the tool cannot read any
  // of its required fields.
  const nlohmann::json message = ExecuteRawToolCall(*agent,
      nlohmann::json({{"id", "call_array_arguments"},
          {"function", {{"name", "ListDirectory"}, {"arguments", "[]"}}}}),
      working_dir.string());
  EXPECT_EQ(message["content"], MissingRequiredFieldMessage("directoryPath"));

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ToolCallWithMissingOrNonStringIdStillExecutes) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_tool_call_id_working_dir_" + std::to_string(++test_id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::create_directories(working_dir);
  {
    std::ofstream file(working_dir / "a.txt");
    file << "hello\n";
  }

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  // A tool call without an `id` must still execute; the agent falls back to an
  // empty `tool_call_id` rather than dropping or crashing on the call.
  const nlohmann::json missing_id = ExecuteRawToolCall(*agent,
      nlohmann::json({{"function", {{"name", "ListDirectory"},
          {"arguments", R"({"directoryPath": "/workspace"})"}}}}),
      working_dir.string());
  EXPECT_EQ(missing_id["role"], "tool");
  EXPECT_EQ(missing_id["tool_call_id"], "");
  const nlohmann::json missing_id_result =
      nlohmann::json::parse(missing_id["content"].get<std::string>());
  ASSERT_TRUE(missing_id_result.contains("entries"));
  EXPECT_EQ(missing_id_result["entries"].size(), 1);

  // A non-string `id` is treated the same way.
  const nlohmann::json numeric_id = ExecuteRawToolCall(*agent,
      nlohmann::json({{"id", 7},
          {"function", {{"name", "ListDirectory"},
              {"arguments", R"({"directoryPath": "/workspace"})"}}}}),
      working_dir.string());
  EXPECT_EQ(numeric_id["role"], "tool");
  EXPECT_EQ(numeric_id["tool_call_id"], "");
  const nlohmann::json numeric_id_result =
      nlohmann::json::parse(numeric_id["content"].get<std::string>());
  ASSERT_TRUE(numeric_id_result.contains("entries"));

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ToolCallWithUnknownFunctionNameIsRejected) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_unknown_function_working_dir_" +
       std::to_string(++test_id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::create_directories(working_dir);

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  // The LLM hallucinated a tool name the agent does not implement. This must be
  // reported back to the LLM instead of crashing the agent.
  const nlohmann::json message = ExecuteRawToolCall(*agent,
      nlohmann::json({{"id", "call_unknown_tool"},
          {"function", {{"name", "TotallyNotATool"}, {"arguments", "{}"}}}}),
      working_dir.string());
  EXPECT_EQ(message["role"], "tool");
  EXPECT_EQ(message["tool_call_id"], "call_unknown_tool");
  EXPECT_EQ(message["content"], "Illegal tool function name.");

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ToolCallsReportMissingRequiredArguments) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_missing_arg_working_dir_" + std::to_string(++test_id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::create_directories(working_dir);
  {
    std::ofstream file(working_dir / "a.txt");
    file << "hello\n";
  }

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  const std::vector<ToolArgumentCase> cases = {
      {"ReadWholeFile", nlohmann::json::object(),
          MissingRequiredFieldMessage("filePath")},
      {"ListDirectory", nlohmann::json::object(),
          MissingRequiredFieldMessage("directoryPath")},
      {"ReadFile", nlohmann::json({{"filePath", "/workspace/a.txt"}}),
          MissingRequiredFieldMessage("beginLine")},
      {"ReadFile",
          nlohmann::json({{"filePath", "/workspace/a.txt"}, {"beginLine", 1}}),
          MissingRequiredFieldMessage("endLine")},
      {"FindFiles", nlohmann::json({{"rootPath", "/workspace"}}),
          MissingRequiredFieldMessage("query")},
      {"FindStrings", nlohmann::json({{"rootPath", "/workspace"}}),
          MissingRequiredFieldMessage("query")},
      {"CreateNewFile", nlohmann::json({{"filePath", "/workspace/new.txt"}}),
          MissingRequiredFieldMessage("initialContent")},
      {"CreateNewDirectory", nlohmann::json::object(),
          MissingRequiredFieldMessage("directoryPath")},
      {"WriteContentToFile",
          nlohmann::json({{"filePath", "/workspace/a.txt"}}),
          MissingRequiredFieldMessage("content")},
      {"SearchAndReplace", nlohmann::json({{"filePath", "/workspace/a.txt"}}),
          MissingRequiredFieldMessage("oldStr")},
      {"SearchAndReplace",
          nlohmann::json({{"filePath", "/workspace/a.txt"},
              {"oldStr", "hello"}}),
          MissingRequiredFieldMessage("newStr")},
      {"Delete", nlohmann::json::object(), MissingRequiredFieldMessage("path")},
      {"Move", nlohmann::json({{"sourcePath", "/workspace/a.txt"}}),
          MissingRequiredFieldMessage("destinationPath")},
      {"Copy", nlohmann::json({{"sourcePath", "/workspace/a.txt"}}),
          MissingRequiredFieldMessage("destinationPath")},
      {"VcsCreateCommit", nlohmann::json::object(),
          MissingRequiredFieldMessage("commitMessage")},
      {"Bash", nlohmann::json::object(),
          MissingRequiredFieldMessage("bashCommand")},
      {"Bash", nlohmann::json({{"bashCommand", "ls"}}),
          MissingRequiredFieldMessage("workingDirectory")},
  };

  for (const ToolArgumentCase& test_case : cases) {
    EXPECT_EQ(ExecuteAgentToolCall(*agent, test_case.tool_name,
        test_case.arguments, working_dir.string()),
        test_case.expected_content) << "tool: " << test_case.tool_name;
  }

  // None of the rejected calls may have created or modified any file.
  EXPECT_TRUE(std::filesystem::exists(working_dir / "a.txt"));
  EXPECT_FALSE(std::filesystem::exists(working_dir / "new.txt"));

  std::filesystem::remove_all(working_dir);
}

TEST_F(JiaolongAgentTest, ToolCallsReportWrongTypedArguments) {
  static int test_id = 0;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_wrong_type_arg_working_dir_" +
       std::to_string(++test_id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::create_directories(working_dir);
  {
    std::ofstream file(working_dir / "a.txt");
    file << "hello\n";
  }

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);

  const std::vector<ToolArgumentCase> cases = {
      {"ReadWholeFile", nlohmann::json({{"filePath", 42}}),
          WrongTypedStringFieldMessage("filePath")},
      {"ListDirectory", nlohmann::json({{"directoryPath", 42}}),
          WrongTypedStringFieldMessage("directoryPath")},
      {"Delete", nlohmann::json({{"path", false}}),
          WrongTypedStringFieldMessage("path")},
      {"ReadFile",
          nlohmann::json({{"filePath", "/workspace/a.txt"},
              {"beginLine", "one"}, {"endLine", 2}}),
          WrongTypedIntFieldMessage("beginLine")},
      {"ReadFile",
          nlohmann::json({{"filePath", "/workspace/a.txt"},
              {"beginLine", 1}, {"endLine", "two"}}),
          WrongTypedIntFieldMessage("endLine")},
      {"WriteContentToFile",
          nlohmann::json({{"filePath", "/workspace/a.txt"}, {"content", 123}}),
          WrongTypedStringFieldMessage("content")},
      {"SearchAndReplace",
          nlohmann::json({{"filePath", "/workspace/a.txt"},
              {"oldStr", "hello"}, {"newStr", 5}}),
          WrongTypedStringFieldMessage("newStr")},
      {"FindFiles",
          nlohmann::json({{"rootPath", "/workspace"}, {"query", 1}}),
          WrongTypedStringFieldMessage("query")},
      {"FindStrings",
          nlohmann::json({{"rootPath", "/workspace"}, {"query", 1}}),
          WrongTypedStringFieldMessage("query")},
      {"Move",
          nlohmann::json({{"sourcePath", "/workspace/a.txt"},
              {"destinationPath", 7}}),
          WrongTypedStringFieldMessage("destinationPath")},
      {"Bash",
          nlohmann::json({{"bashCommand", "ls"}, {"workingDirectory", 1}}),
          WrongTypedStringFieldMessage("workingDirectory")},
      {"VcsCreateCommit", nlohmann::json({{"commitMessage", 1}}),
          WrongTypedStringFieldMessage("commitMessage")},
  };

  for (const ToolArgumentCase& test_case : cases) {
    EXPECT_EQ(ExecuteAgentToolCall(*agent, test_case.tool_name,
        test_case.arguments, working_dir.string()),
        test_case.expected_content) << "tool: " << test_case.tool_name;
  }

  // The file must be untouched: no rejected call may have written to it.
  {
    std::ifstream victim_stream(working_dir / "a.txt");
    std::string victim_content;
    std::getline(victim_stream, victim_content);
    EXPECT_EQ(victim_content, "hello");
  }

  std::filesystem::remove_all(working_dir);
}

// ---------------------------------------------------------------------------
// Multiple folders (read-only mounts).
//
// A task may select a subset of its project's readonly directories. The agent
// must be able to read the selected `/readonly/<alias>` mounts but never write
// to them, and an unselected project readonly directory must be unreachable.
// ---------------------------------------------------------------------------

namespace {

// Builds a project whose catalog has `docs` and `libs`, and a task working on
// `working_dir` that selects the given catalog ids.
Project BuildReadonlyProject(const std::filesystem::path& docs_dir,
    const std::filesystem::path& libs_dir) {
  Project project;
  project.id_ = "project_1";
  ReadonlyDirectory docs;
  docs.id_ = "dir_docs";
  docs.alias_ = "docs";
  docs.real_path_ = docs_dir.string();
  docs.description_ = "Public API docs";
  ReadonlyDirectory libs;
  libs.id_ = "dir_libs";
  libs.alias_ = "libs";
  libs.real_path_ = libs_dir.string();
  libs.description_ = "Shared library headers";
  project.readonly_directories_ = {docs, libs};
  return project;
}

}  // namespace

TEST_F(JiaolongAgentTest, TaskFromJsonParsesOptionalReadonlyDirectoryIds) {
  const nlohmann::json base = {
      {"id", "task_1"},
      {"title", "title"},
      {"description", "description"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 10}};

  // Absent `readonlyDirectoryIds` => empty selection.
  const std::optional<Task> absent = Task::FromJson(base);
  ASSERT_TRUE(absent.has_value());
  EXPECT_TRUE(absent->readonly_directories_.empty());

  // Present array of strings => parsed in order.
  nlohmann::json with_ids = base;
  with_ids["readonlyDirectoryIds"] = {"dir_docs", "dir_libs"};
  const std::optional<Task> present = Task::FromJson(with_ids);
  ASSERT_TRUE(present.has_value());
  ASSERT_EQ(present->readonly_directories_.size(), 2u);
  EXPECT_EQ(present->readonly_directories_[0], "dir_docs");
  EXPECT_EQ(present->readonly_directories_[1], "dir_libs");

  // Non-array field => rejected.
  nlohmann::json not_array = base;
  not_array["readonlyDirectoryIds"] = "dir_docs";
  EXPECT_FALSE(Task::FromJson(not_array).has_value());

  // Array with a non-string entry => rejected.
  nlohmann::json bad_entry = base;
  bad_entry["readonlyDirectoryIds"] = {1};
  EXPECT_FALSE(Task::FromJson(bad_entry).has_value());
}

TEST_F(JiaolongAgentTest, ReadToolsCanReadSelectedReadonlyDirectory) {
  static int test_id = 0;
  const int id = ++test_id;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_readonly_working_dir_" + std::to_string(id));
  const std::filesystem::path docs_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_readonly_docs_dir_" + std::to_string(id));
  const std::filesystem::path libs_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_readonly_libs_dir_" + std::to_string(id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(docs_dir);
  std::filesystem::remove_all(libs_dir);
  std::filesystem::create_directories(working_dir);
  std::filesystem::create_directories(docs_dir);
  std::filesystem::create_directories(libs_dir);
  {
    std::ofstream file(docs_dir / "api.md");
    file << "# API\n";
  }

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);
  agent->ConfigureMountTable(
      Task("task_1", "title", "description", working_dir.string(),
          /*token_limit=*/1000, {"dir_docs"}),
      BuildReadonlyProject(docs_dir, libs_dir));

  // A selected readonly directory can be read through its virtual root.
  const std::string read_result = ExecuteAgentToolCall(*agent, "ReadWholeFile",
      nlohmann::json{{"filePath", "/readonly/docs/api.md"}},
      working_dir.string());
  const nlohmann::json read_json =
      nlohmann::json::parse(read_result, nullptr, false);
  ASSERT_FALSE(read_json.is_discarded());
  ASSERT_TRUE(read_json.contains("fileContent"));
  EXPECT_EQ(read_json["fileContent"], "# API\n");

  // Listing the selected readonly directory works too.
  const std::string list_result = ExecuteAgentToolCall(*agent, "ListDirectory",
      nlohmann::json{{"directoryPath", "/readonly/docs"}}, working_dir.string());
  const nlohmann::json list_json =
      nlohmann::json::parse(list_result, nullptr, false);
  ASSERT_FALSE(list_json.is_discarded());
  ASSERT_TRUE(list_json.contains("entries"));
  ASSERT_EQ(list_json["entries"].size(), 1u);
  EXPECT_EQ(list_json["entries"][0]["name"], "api.md");

  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(docs_dir);
  std::filesystem::remove_all(libs_dir);
}

TEST_F(JiaolongAgentTest, ReadToolsRejectUnselectedReadonlyDirectory) {
  static int test_id = 0;
  const int id = ++test_id;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_unselected_working_dir_" + std::to_string(id));
  const std::filesystem::path docs_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_unselected_docs_dir_" + std::to_string(id));
  const std::filesystem::path libs_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_unselected_libs_dir_" + std::to_string(id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(docs_dir);
  std::filesystem::remove_all(libs_dir);
  std::filesystem::create_directories(working_dir);
  std::filesystem::create_directories(docs_dir);
  std::filesystem::create_directories(libs_dir);
  {
    std::ofstream file(libs_dir / "x.h");
    file << "int x;\n";
  }

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);
  // Only `docs` is selected; `libs` stays unreachable for this task.
  agent->ConfigureMountTable(
      Task("task_1", "title", "description", working_dir.string(),
          /*token_limit=*/1000, {"dir_docs"}),
      BuildReadonlyProject(docs_dir, libs_dir));

  EXPECT_EQ(ExecuteAgentToolCall(*agent, "ReadWholeFile",
      nlohmann::json{{"filePath", "/readonly/libs/x.h"}}, working_dir.string()),
      "invalid input, error_message: invalid file path.");

  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(docs_dir);
  std::filesystem::remove_all(libs_dir);
}

TEST_F(JiaolongAgentTest, WriteToolsRejectReadonlyDirectory) {
  static int test_id = 0;
  const int id = ++test_id;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_ro_write_working_dir_" + std::to_string(id));
  const std::filesystem::path docs_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_ro_write_docs_dir_" + std::to_string(id));
  const std::filesystem::path libs_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_ro_write_libs_dir_" + std::to_string(id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(docs_dir);
  std::filesystem::remove_all(libs_dir);
  std::filesystem::create_directories(working_dir);
  std::filesystem::create_directories(docs_dir);
  std::filesystem::create_directories(libs_dir);
  {
    std::ofstream file(docs_dir / "api.md");
    file << "# API\n";
  }

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);
  agent->ConfigureMountTable(
      Task("task_1", "title", "description", working_dir.string(),
          /*token_limit=*/1000, {"dir_docs"}),
      BuildReadonlyProject(docs_dir, libs_dir));

  // Every write tool must refuse to touch the readonly mount.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "WriteContentToFile",
      nlohmann::json{{"filePath", "/readonly/docs/api.md"},
          {"content", "hacked"}}, working_dir.string()),
      "invalid input, error_message: forbidden file path.");
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "SearchAndReplace",
      nlohmann::json{{"filePath", "/readonly/docs/api.md"},
          {"oldStr", "API"}, {"newStr", "HACKED"}}, working_dir.string()),
      "invalid input, error_message: forbidden file path.");
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "Delete",
      nlohmann::json{{"path", "/readonly/docs/api.md"}}, working_dir.string()),
      "invalid input, error_message: forbidden path.");
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "Move",
      nlohmann::json{{"sourcePath", "/readonly/docs/api.md"},
          {"destinationPath", "/workspace/api.md"}}, working_dir.string()),
      "invalid input, error_message: forbidden path.");

  // The readonly file must be completely untouched.
  {
    std::ifstream stream(docs_dir / "api.md");
    std::string content;
    std::getline(stream, content);
    EXPECT_EQ(content, "# API");
  }
  EXPECT_TRUE(std::filesystem::exists(docs_dir / "api.md"));
  EXPECT_FALSE(std::filesystem::exists(working_dir / "api.md"));

  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(docs_dir);
  std::filesystem::remove_all(libs_dir);
}

TEST_F(JiaolongAgentTest, CopyBridgesReadonlyFileIntoWorkspace) {
  static int test_id = 0;
  const int id = ++test_id;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_copy_working_dir_" + std::to_string(id));
  const std::filesystem::path docs_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_copy_docs_dir_" + std::to_string(id));
  const std::filesystem::path libs_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_copy_libs_dir_" + std::to_string(id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(docs_dir);
  std::filesystem::remove_all(libs_dir);
  std::filesystem::create_directories(working_dir);
  std::filesystem::create_directories(docs_dir);
  std::filesystem::create_directories(libs_dir);
  {
    std::ofstream file(docs_dir / "api.md");
    file << "# API\n";
  }

  std::shared_ptr<Agent> agent = CreateTestAgent();
  agent->SetToolPermission(ToolPermission::kAllowAll);
  agent->ConfigureMountTable(
      Task("task_1", "title", "description", working_dir.string(),
          /*token_limit=*/1000, {"dir_docs"}),
      BuildReadonlyProject(docs_dir, libs_dir));

  // Copy from the RO mount into the workspace is allowed...
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "Copy",
      nlohmann::json{{"sourcePath", "/readonly/docs/api.md"},
          {"destinationPath", "/workspace/api.md"}}, working_dir.string()),
      "success");
  ASSERT_TRUE(std::filesystem::exists(working_dir / "api.md"));
  {
    std::ifstream stream(working_dir / "api.md");
    std::string content;
    std::getline(stream, content);
    EXPECT_EQ(content, "# API");
  }

  // ...and the copy inside the workspace can then be edited.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "SearchAndReplace",
      nlohmann::json{{"filePath", "/workspace/api.md"},
          {"oldStr", "API"}, {"newStr", "LOCAL"}}, working_dir.string()),
      "success");

  // Copying the other way (RW -> RO) is denied.
  EXPECT_EQ(ExecuteAgentToolCall(*agent, "Copy",
      nlohmann::json{{"sourcePath", "/workspace/api.md"},
          {"destinationPath", "/readonly/docs/api2.md"}}, working_dir.string()),
      "invalid input, error_message: forbidden path.");

  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(docs_dir);
  std::filesystem::remove_all(libs_dir);
}

TEST_F(JiaolongAgentTest, WorkDisclosesSelectedReadonlyDirectoriesToTheLlm) {
  FakeLlmApiServer server({200});
  ASSERT_TRUE(server.Start());

  Agent agent(FakeLlmApiBaseUrl(server), "test-model", "test-security-key",
      /*llm_cooldown_duration=*/ 0,
      /*llm_retry_times=*/ 0,
      /*jj_executable_path=*/ "test_jj",
      /*github_token=*/ "test_token",
      /*gh_executable_path=*/ "test_gh",
      /*allowed_bash_commands=*/ {},
      /*socks_proxy=*/ "test_socks_proxy",
      /*forbidden_to_write_file_list=*/ {});
  agent.SetToolPermission(ToolPermission::kAllowAll);

  static int test_id = 0;
  const int id = ++test_id;
  const std::filesystem::path working_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_layout_working_dir_" + std::to_string(id));
  const std::filesystem::path docs_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_layout_docs_dir_" + std::to_string(id));
  const std::filesystem::path libs_dir =
      std::filesystem::temp_directory_path() /
      ("jiaolong_agent_layout_libs_dir_" + std::to_string(id));
  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(docs_dir);
  std::filesystem::remove_all(libs_dir);
  std::filesystem::create_directories(working_dir);
  std::filesystem::create_directories(docs_dir);
  std::filesystem::create_directories(libs_dir);

  const Task task("task_1", "title", "description", working_dir.string(),
      /*token_limit=*/1000, {"dir_docs"});
  ASSERT_TRUE(agent.Work(task, BuildReadonlyProject(docs_dir, libs_dir)));

  const std::string system_prompt = agent.messages_[0]["content"].get<std::string>();
  EXPECT_NE(system_prompt.find("/readonly/docs"), std::string::npos);
  // The selected directory's description is disclosed, its real host path is
  // not.
  EXPECT_NE(system_prompt.find("Public API docs"), std::string::npos);
  EXPECT_EQ(system_prompt.find(docs_dir.string()), std::string::npos);
  // An unselected project readonly directory is not disclosed.
  EXPECT_EQ(system_prompt.find("/readonly/libs"), std::string::npos);

  std::filesystem::remove_all(working_dir);
  std::filesystem::remove_all(docs_dir);
  std::filesystem::remove_all(libs_dir);
}

}  // namespace
}  // namespace jiaolong
