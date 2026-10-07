#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <unistd.h>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "server/jiaolong_server.h"
#include "server/task_database.h"

namespace jiaolong {
namespace server {
namespace {

// Creates the tables the production code assumes already exist (see
// doc/technology/database_design.md). TaskDatabase and ProjectDatabase never
// create or migrate the schema at runtime, so the tests set it up explicitly.
void CreateSchema(const std::string& db_path) {
  // The database file lives in ~/.jiaolong, which does not exist in the fresh
  // temporary HOME, so create the parent directory before opening the
  // database; otherwise sqlite3_open fails with SQLITE_CANTOPEN.
  const std::filesystem::path path(db_path);
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open(db_path.c_str(), &db), SQLITE_OK);
  static const char kCreateSchema[] =
      "CREATE TABLE tasks ("
      "  id                   TEXT PRIMARY KEY,"
      "  title                TEXT NOT NULL,"
      "  description          TEXT NOT NULL DEFAULT '',"
      "  working_directory    TEXT NOT NULL,"
      "  token_limit          INTEGER NOT NULL DEFAULT 0,"
      "  priority             TEXT NOT NULL DEFAULT 'normal',"
      "  state                TEXT NOT NULL DEFAULT 'todo',"
      "  current_round        INTEGER NOT NULL DEFAULT 0,"
      "  agent_working        INTEGER NOT NULL DEFAULT 0,"
      "  created_at           TEXT NOT NULL,"
      "  updated_at           TEXT NOT NULL,"
      "  total_token_usage    INTEGER NOT NULL DEFAULT 0,"
      "  project_id           TEXT NOT NULL DEFAULT '',"
      "  readonly_directories TEXT NOT NULL DEFAULT ''"
      ");"
      "CREATE TABLE projects ("
      "  id                             TEXT PRIMARY KEY,"
      "  name                           TEXT NOT NULL,"
      "  description                    TEXT NOT NULL DEFAULT '',"
      "  default_task_working_directory TEXT NOT NULL DEFAULT '',"
      "  ordering                       INTEGER NOT NULL DEFAULT 0"
      ");"
      "CREATE TABLE project_readonly_directories ("
      "  id           TEXT PRIMARY KEY,"
      "  project_id   TEXT NOT NULL,"
      "  alias        TEXT NOT NULL,"
      "  real_path    TEXT NOT NULL,"
      "  description  TEXT NOT NULL DEFAULT '',"
      "  ordering     INTEGER NOT NULL DEFAULT 0,"
      "  UNIQUE(project_id, alias)"
      ");";
  char* error_message = nullptr;
  ASSERT_EQ(sqlite3_exec(db, kCreateSchema, nullptr, nullptr, &error_message),
            SQLITE_OK);
  sqlite3_free(error_message);
  sqlite3_close(db);
}

// Points HOME at a fresh temporary directory for the lifetime of the object so
// that the default database path (~/.jiaolong/database.sqlite) resolves inside
// it, and restores the previous value on destruction. JiaolongServer opens the
// default database through its own members, so a test-only database path
// cannot be injected; pointing HOME at a temporary directory is what keeps the
// test from touching the real database.
class ScopedHomeDirectory {
 public:
  ScopedHomeDirectory() {
    const char* previous_home = std::getenv("HOME");
    if (previous_home != nullptr) {
      previous_home_ = previous_home;
    }
    static int test_counter = 0;
    directory_ = std::filesystem::temp_directory_path() /
        ("jiaolong_server_test_home_" + std::to_string(::getpid()) + "_" +
         std::to_string(++test_counter));
    std::error_code remove_error;
    std::filesystem::remove_all(directory_, remove_error);
    std::filesystem::create_directories(directory_);
    setenv("HOME", directory_.c_str(), 1);
  }

  ~ScopedHomeDirectory() {
    if (previous_home_.empty()) {
      unsetenv("HOME");
    } else {
      setenv("HOME", previous_home_.c_str(), 1);
    }
    std::error_code remove_error;
    std::filesystem::remove_all(directory_, remove_error);
  }

  ScopedHomeDirectory(const ScopedHomeDirectory&) = delete;
  ScopedHomeDirectory& operator=(const ScopedHomeDirectory&) = delete;

  const std::filesystem::path& path() const { return directory_; }

 private:
  std::filesystem::path directory_;
  std::string previous_home_;
};

// Binds the given server to a free ephemeral port, runs its accept loop on a
// background thread and stops/joins it on destruction. Making the runner own
// the thread guarantees the server is shut down even when an assertion aborts
// the test body early.
class RunningHttpServer {
 public:
  explicit RunningHttpServer(httplib::Server& server) : server_(server) {
    port_ = server_.bind_to_any_port("127.0.0.1");
    if (port_ <= 0) {
      return;
    }
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    server_.wait_until_ready();
  }

  ~RunningHttpServer() {
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  RunningHttpServer(const RunningHttpServer&) = delete;
  RunningHttpServer& operator=(const RunningHttpServer&) = delete;

  int port() const { return port_; }

 private:
  httplib::Server& server_;
  std::thread thread_;
  int port_ = -1;
};

class JiaolongServerHttpApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    home_ = std::make_unique<ScopedHomeDirectory>();
    CreateSchema((home_->path() / ".jiaolong" / "database.sqlite").string());
  }

  std::unique_ptr<ScopedHomeDirectory> home_;
};

// The search API must be reachable over HTTP: a request to
// GET /api/tasks/search must be routed to the search implementation (and not
// captured by the /api/tasks/:id task-detail route) and return the tasks of
// the requested project whose title or description matches every keyword.
TEST_F(JiaolongServerHttpApiTest, SearchApiRoutesToSearchImplementation) {
  constexpr char kUsername[] = "test_user";
  constexpr char kPassword[] = "test_password";

  // Constructing the server opens the database at the (temporary) HOME and
  // starts the system-health perception loop.
  JiaolongServer server("", "", "", "", "", "127.0.0.1", kUsername, kPassword);

  // Seed the task database through the server's own database handle: two tasks
  // in project_a and one in project_b sharing a matching keyword, so the test
  // can also verify that results are scoped to the requested project.
  Task login_task;
  login_task.project_id = "project_a";
  login_task.title = "Fix login bug";
  login_task.description = "Users cannot log in";
  login_task.working_directory = "/tmp";
  login_task.token_limit = 1000;
  ASSERT_FALSE(server.GetTaskDatabase().CreateTask(login_task).empty());

  Task payment_task;
  payment_task.project_id = "project_a";
  payment_task.title = "Add payment flow";
  payment_task.description = "Implement checkout";
  payment_task.working_directory = "/tmp";
  payment_task.token_limit = 1000;
  ASSERT_FALSE(server.GetTaskDatabase().CreateTask(payment_task).empty());

  Task other_project_task;
  other_project_task.project_id = "project_b";
  other_project_task.title = "Fix login bug in another project";
  other_project_task.description = "";
  other_project_task.working_directory = "/tmp";
  other_project_task.token_limit = 1000;
  ASSERT_FALSE(server.GetTaskDatabase().CreateTask(other_project_task).empty());

  server.RegisterRoutes();

  RunningHttpServer running_server(server.GetHttpServer());
  const int port = running_server.port();
  ASSERT_GT(port, 0);

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(0, 100000 /* usec */);
  client.set_read_timeout(5, 0);

  // Every route except login/refresh is protected by a bearer token, so log in
  // as the configured client to obtain one (retrying in case the connection is
  // not yet established).
  const std::string login_body =
      nlohmann::json{{"username", kUsername}, {"password", kPassword}}.dump();
  std::string access_token;
  for (int attempt = 0; attempt < 500 && access_token.empty(); ++attempt) {
    const httplib::Result response =
        client.Post("/api/auth/login", login_body, "application/json");
    if (response && response->status == 200) {
      const nlohmann::json body =
          nlohmann::json::parse(response->body, nullptr, false);
      if (!body.is_discarded() && body.contains("accessToken") &&
          body["accessToken"].is_string()) {
        access_token = body["accessToken"].get<std::string>();
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_FALSE(access_token.empty());
  client.set_default_headers({{"Authorization", "Bearer " + access_token}});

  // Builds a search request with the given parameters.
  const auto search = [&client](const std::string& project_id,
                                const std::string& keywords) {
    return client.Get(
        "/api/tasks/search",
        httplib::Params{{"projectId", project_id}, {"keywords", keywords}});
  };

  // Title match, scoped to the requested project. If the request were captured
  // by the /api/tasks/:id route it would look up a task with the id "search"
  // and answer 404 instead.
  const httplib::Result title_match = search("project_a", "login");
  ASSERT_TRUE(title_match);
  EXPECT_EQ(title_match->status, 200);
  const nlohmann::json title_match_body =
      nlohmann::json::parse(title_match->body, nullptr, false);
  ASSERT_FALSE(title_match_body.is_discarded());
  ASSERT_TRUE(title_match_body.is_array());
  ASSERT_EQ(title_match_body.size(), 1u);
  EXPECT_EQ(title_match_body[0]["title"], "Fix login bug");
  EXPECT_EQ(title_match_body[0]["projectId"], "project_a");

  // Description match, case-insensitive.
  const httplib::Result description_match = search("project_a", "CHECKOUT");
  ASSERT_TRUE(description_match);
  ASSERT_EQ(description_match->status, 200);
  const nlohmann::json description_match_body =
      nlohmann::json::parse(description_match->body, nullptr, false);
  ASSERT_FALSE(description_match_body.is_discarded());
  ASSERT_TRUE(description_match_body.is_array());
  ASSERT_EQ(description_match_body.size(), 1u);
  EXPECT_EQ(description_match_body[0]["title"], "Add payment flow");

  // Every whitespace-separated keyword must match.
  const httplib::Result all_keywords = search("project_a", "login bug");
  ASSERT_TRUE(all_keywords);
  ASSERT_EQ(all_keywords->status, 200);
  const nlohmann::json all_keywords_body =
      nlohmann::json::parse(all_keywords->body, nullptr, false);
  ASSERT_TRUE(all_keywords_body.is_array());
  ASSERT_EQ(all_keywords_body.size(), 1u);
  EXPECT_EQ(all_keywords_body[0]["title"], "Fix login bug");

  // A missing keyword yields no result.
  const httplib::Result missing_keyword =
      search("project_a", "login payment");
  ASSERT_TRUE(missing_keyword);
  ASSERT_EQ(missing_keyword->status, 200);
  const nlohmann::json missing_keyword_body =
      nlohmann::json::parse(missing_keyword->body, nullptr, false);
  ASSERT_TRUE(missing_keyword_body.is_array());
  EXPECT_TRUE(missing_keyword_body.empty());

  // An empty keywords string returns every task of the project (and only that
  // project's tasks).
  const httplib::Result empty_keywords = search("project_a", "");
  ASSERT_TRUE(empty_keywords);
  ASSERT_EQ(empty_keywords->status, 200);
  const nlohmann::json empty_keywords_body =
      nlohmann::json::parse(empty_keywords->body, nullptr, false);
  ASSERT_TRUE(empty_keywords_body.is_array());
  EXPECT_EQ(empty_keywords_body.size(), 2u);

  // A request without a projectId is rejected as a bad request by the search
  // handler itself. This also proves the request reached that handler: the
  // /api/tasks/:id route would instead report 404 for the id "search".
  const httplib::Result missing_project = client.Get("/api/tasks/search");
  ASSERT_TRUE(missing_project);
  EXPECT_EQ(missing_project->status, 400);
}

}  // namespace
}  // namespace server
}  // namespace jiaolong