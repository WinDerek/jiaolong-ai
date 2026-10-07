#include <filesystem>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "server/project_database.h"
#include "server/task_database.h"
#include "server/task_service.h"

namespace jiaolong {
namespace server {
namespace {

// Creates the tables the production code assumes already exist (see
// doc/database_design.md). TaskDatabase and ProjectDatabase never create or
// migrate the schema at runtime, so the tests set it up explicitly.
void CreateSchema(const std::string& db_path) {
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

class TaskServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static int test_id = 0;
    db_path_ = std::filesystem::temp_directory_path() /
        ("jiaolong_task_service_test_" + std::to_string(++test_id) + ".sqlite");
    CreateSchema(db_path_.string());
  }

  void TearDown() override {
    std::filesystem::remove_all(db_path_);
  }

  std::filesystem::path db_path_;
};

// Creates and starts a task (state in_progress, agent_working true), which
// models a task right after the server launched the agent subprocess. Returns
// the task id.
std::string CreateStartedTask(TaskDatabase& database) {
  Task task;
  task.id = "test_task_id";
  task.title = "Test task";
  task.description = "Test description";
  task.working_directory = "/tmp";
  task.token_limit = 1000;
  task.priority = "normal";
  const std::string id = database.CreateTask(task);
  database.StartWork(id);
  return id;
}

TEST_F(TaskServiceTest, CompleteWorkMovesTaskToNeedsReviewWhenLlmStopped) {
  TaskDatabase database(db_path_.string());
  const std::string id = CreateStartedTask(database);
  TaskService service(database);

  // The agent subprocess finished and the LLM stopped naturally: the task
  // must be moved to needs_review and agent_working must be cleared.
  const TaskServiceResult result = service.CompleteWork(id);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.value["state"], "needs_review");
  EXPECT_EQ(result.value["agentWorking"], false);

  const std::optional<Task> updated = database.GetTask(id);
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->state, "needs_review");
  EXPECT_FALSE(updated->agent_working);
}

TEST_F(TaskServiceTest, FailWorkMovesTaskToFailedWhenLlmDidNotStop) {
  TaskDatabase database(db_path_.string());
  const std::string id = CreateStartedTask(database);
  TaskService service(database);

  // The agent subprocess finished but the LLM did not stop naturally: the
  // task must be moved to failed and agent_working must be cleared.
  const TaskServiceResult result = service.FailWork(id);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.value["state"], "failed");
  EXPECT_EQ(result.value["agentWorking"], false);

  const std::optional<Task> updated = database.GetTask(id);
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->state, "failed");
  EXPECT_FALSE(updated->agent_working);
}

TEST_F(TaskServiceTest, CompleteWorkAndFailWorkReportNotFoundForMissingTask) {
  TaskDatabase database(db_path_.string());
  TaskService service(database);

  const TaskServiceResult complete_result = service.CompleteWork("missing");
  EXPECT_FALSE(complete_result.success);
  EXPECT_EQ(complete_result.error_type, TaskServiceErrorType::kNotFound);

  const TaskServiceResult fail_result = service.FailWork("missing");
  EXPECT_FALSE(fail_result.success);
  EXPECT_EQ(fail_result.error_type, TaskServiceErrorType::kNotFound);
}

TEST_F(TaskServiceTest, CreateTaskInitializesTotalTokenUsageToZeroAndExposesIt) {
  TaskDatabase database(db_path_.string());
  TaskService service(database);

  const TaskServiceResult result = service.CreateTask(nlohmann::json{
      {"projectId", "project_1"},
      {"title", "Test task"},
      {"description", "Test description"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 1000},
  });
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.value.contains("totalTokenUsage"));
  EXPECT_EQ(result.value["totalTokenUsage"], 0);
}

TEST_F(TaskServiceTest, GetTotalTokenUsageSumsAllTasks) {
  TaskDatabase database(db_path_.string());
  TaskService service(database);

  // With no tasks the total token usage is 0.
  const TaskServiceResult empty_result = service.GetTotalTokenUsage();
  ASSERT_TRUE(empty_result.success);
  EXPECT_EQ(empty_result.value["totalTokenUsage"], 0);

  // Create two tasks.
  Task task_a;
  task_a.title = "Task A";
  task_a.working_directory = "/tmp";
  task_a.token_limit = 1000;
  const std::string id_a = database.CreateTask(task_a);
  ASSERT_FALSE(id_a.empty());

  Task task_b;
  task_b.title = "Task B";
  task_b.working_directory = "/tmp";
  task_b.token_limit = 1000;
  const std::string id_b = database.CreateTask(task_b);
  ASSERT_FALSE(id_b.empty());

  // Set total_token_usage directly, the same way the agent process updates
  // the column after a successful run, then check that the service sums the
  // values over all tasks.
  {
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(db_path_.string().c_str(), &db), SQLITE_OK);
    char* error_message = nullptr;
    std::string sql_statement = "UPDATE tasks SET total_token_usage = 123 WHERE id = '" + id_a + "';" +
        "UPDATE tasks SET total_token_usage = 77 WHERE id = '" + id_b + "';";
    const int update_rc = sqlite3_exec(db,
        sql_statement.c_str(),
        nullptr, nullptr, &error_message);
    ASSERT_EQ(update_rc, SQLITE_OK);
    sqlite3_free(error_message);
    sqlite3_close(db);
  }

  const TaskServiceResult result = service.GetTotalTokenUsage();
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.value["totalTokenUsage"], 200);
}

TEST_F(TaskServiceTest, GetTaskReturnsLatestTaskAndReportsNotFound) {
  TaskDatabase database(db_path_.string());
  TaskService service(database);

  // A missing task is reported as not found.
  const TaskServiceResult missing_result = service.GetTask("missing");
  EXPECT_FALSE(missing_result.success);
  EXPECT_EQ(missing_result.error_type, TaskServiceErrorType::kNotFound);

  // Create a task; GetTask must return the full task as JSON, including the
  // fields that back the task detail page.
  const TaskServiceResult create_result = service.CreateTask(nlohmann::json{
      {"projectId", "project_1"},
      {"title", "Test task"},
      {"description", "Test description"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 1000},
  });
  ASSERT_TRUE(create_result.success);
  const std::string id = create_result.value["id"].get<std::string>();
  ASSERT_FALSE(id.empty());

  // Mutate the task the way the server does when the agent starts working and
  // check that GetTask returns the newest server-side state.
  const TaskServiceResult work_result = service.StartWork(id);
  ASSERT_TRUE(work_result.success);

  const TaskServiceResult get_result = service.GetTask(id);
  ASSERT_TRUE(get_result.success);
  EXPECT_EQ(get_result.value["id"].get<std::string>(), id);
  EXPECT_EQ(get_result.value["state"].get<std::string>(), "in_progress");
  EXPECT_EQ(get_result.value["agentWorking"].get<bool>(), true);
}

TEST_F(TaskServiceTest, CreateTaskRequiresProjectId) {
  TaskDatabase database(db_path_.string());
  TaskService service(database);

  // A task must always be created under a project, so a request without a
  // projectId is rejected as invalid input.
  const TaskServiceResult result = service.CreateTask(nlohmann::json{
      {"title", "Test task"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 1000},
  });
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_type, TaskServiceErrorType::kInvalidInput);
}

TEST_F(TaskServiceTest, CreateTaskStoresProjectIdAndListTasksScopesToProject) {
  TaskDatabase database(db_path_.string());
  TaskService service(database);

  const TaskServiceResult created_a = service.CreateTask(nlohmann::json{
      {"projectId", "project_a"},
      {"title", "Task A"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 1000},
  });
  ASSERT_TRUE(created_a.success);
  EXPECT_EQ(created_a.value["projectId"], "project_a");

  const TaskServiceResult created_b = service.CreateTask(nlohmann::json{
      {"projectId", "project_b"},
      {"title", "Task B"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 1000},
  });
  ASSERT_TRUE(created_b.success);

  // The task list is scoped to the requested project.
  const TaskServiceResult list_a = service.ListTasks("project_a", "", "", "");
  ASSERT_TRUE(list_a.success);
  ASSERT_EQ(list_a.value.size(), 1);
  EXPECT_EQ(list_a.value[0]["title"], "Task A");
  EXPECT_EQ(list_a.value[0]["projectId"], "project_a");
}

TEST_F(TaskServiceTest, CreateTaskStoresReadonlyDirectoryIds) {
  TaskDatabase database(db_path_.string());
  TaskService service(database);

  const TaskServiceResult result = service.CreateTask(nlohmann::json{
      {"projectId", "project_1"},
      {"title", "Test task"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 1000},
      {"readonlyDirectoryIds", {"dir_a", "dir_b"}},
  });
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.value.contains("readonlyDirectoryIds"));
  ASSERT_EQ(result.value["readonlyDirectoryIds"].size(), 2u);
  EXPECT_EQ(result.value["readonlyDirectoryIds"][0], "dir_a");
  EXPECT_EQ(result.value["readonlyDirectoryIds"][1], "dir_b");

  // The selection round-trips through the database.
  const std::string id = result.value["id"].get<std::string>();
  const std::optional<Task> stored = database.GetTask(id);
  ASSERT_TRUE(stored.has_value());
  ASSERT_EQ(stored->readonly_directories.size(), 2u);
  EXPECT_EQ(stored->readonly_directories[0], "dir_a");
  EXPECT_EQ(stored->readonly_directories[1], "dir_b");
}

TEST_F(TaskServiceTest, CreateTaskRejectsMalformedReadonlyDirectoryIds) {
  TaskDatabase database(db_path_.string());
  TaskService service(database);

  const TaskServiceResult not_array = service.CreateTask(nlohmann::json{
      {"projectId", "project_1"},
      {"title", "Test task"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 1000},
      {"readonlyDirectoryIds", "dir_a"},
  });
  EXPECT_FALSE(not_array.success);
  EXPECT_EQ(not_array.error_type, TaskServiceErrorType::kInvalidInput);

  const TaskServiceResult duplicate = service.CreateTask(nlohmann::json{
      {"projectId", "project_1"},
      {"title", "Test task"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 1000},
      {"readonlyDirectoryIds", {"dir_a", "dir_a"}},
  });
  EXPECT_FALSE(duplicate.success);
  EXPECT_EQ(duplicate.error_type, TaskServiceErrorType::kInvalidInput);
}

TEST_F(TaskServiceTest, CreateTaskValidatesReadonlyDirectorySubset) {
  TaskDatabase database(db_path_.string());
  ProjectDatabase project_database(db_path_.string());

  Project project;
  project.id = "project_1";
  project.name = "Project";
  project.default_task_working_directory = "/tmp";
  ReadonlyDirectory docs;
  docs.alias = "docs";
  docs.real_path = "/tmp/docs";
  docs.description = "Docs";
  project.readonly_directories = {docs};
  ASSERT_EQ(project_database.CreateProject(project), "project_1");

  const std::optional<Project> stored =
      project_database.GetProject("project_1");
  ASSERT_TRUE(stored.has_value());
  ASSERT_EQ(stored->readonly_directories.size(), 1u);
  const std::string docs_id = stored->readonly_directories[0].id;
  ASSERT_FALSE(docs_id.empty());

  TaskService service(database, project_database);

  const TaskServiceResult valid = service.CreateTask(nlohmann::json{
      {"projectId", "project_1"},
      {"title", "Test task"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 1000},
      {"readonlyDirectoryIds", {docs_id}},
  });
  ASSERT_TRUE(valid.success);
  ASSERT_EQ(valid.value["readonlyDirectoryIds"].size(), 1u);
  EXPECT_EQ(valid.value["readonlyDirectoryIds"][0], docs_id);

  const TaskServiceResult unknown = service.CreateTask(nlohmann::json{
      {"projectId", "project_1"},
      {"title", "Test task"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 1000},
      {"readonlyDirectoryIds", {"does_not_exist"}},
  });
  EXPECT_FALSE(unknown.success);
  EXPECT_EQ(unknown.error_type, TaskServiceErrorType::kInvalidInput);
}

TEST_F(TaskServiceTest, UpdateTaskUpdatesProjectDefaultWorkingDirectory) {
  TaskDatabase database(db_path_.string());
  ProjectDatabase project_database(db_path_.string());

  Project project;
  project.id = "project_1";
  project.name = "Project";
  project.default_task_working_directory = "/tmp";
  ASSERT_EQ(project_database.CreateProject(project), "project_1");

  TaskService service(database, project_database);

  const TaskServiceResult created = service.CreateTask(nlohmann::json{
      {"projectId", "project_1"},
      {"title", "Test task"},
      {"workingDirectory", "/tmp/created"},
      {"tokenLimit", 1000},
  });
  ASSERT_TRUE(created.success);
  const std::string id = created.value["id"].get<std::string>();
  ASSERT_FALSE(id.empty());

  // Editing the task's working directory must keep the associated project's
  // default task working directory in sync, just like creation does.
  const TaskServiceResult updated = service.UpdateTask(
      id, nlohmann::json{{"workingDirectory", "/tmp/edited"}});
  ASSERT_TRUE(updated.success);
  EXPECT_EQ(updated.value["workingDirectory"], "/tmp/edited");

  const std::optional<Project> stored =
      project_database.GetProject("project_1");
  ASSERT_TRUE(stored.has_value());
  EXPECT_EQ(stored->default_task_working_directory, "/tmp/edited");
}

TEST_F(TaskServiceTest, SearchTasksMatchesTitleAndDescriptionByKeywords) {
  TaskDatabase database(db_path_.string());
  TaskService service(database);

  const TaskServiceResult login_task = service.CreateTask(nlohmann::json{
      {"projectId", "project_a"},
      {"title", "Fix login bug"},
      {"description", "Users cannot log in"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 1000},
  });
  ASSERT_TRUE(login_task.success);

  const TaskServiceResult payment_task = service.CreateTask(nlohmann::json{
      {"projectId", "project_a"},
      {"title", "Add payment flow"},
      {"description", "Implement checkout"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 1000},
  });
  ASSERT_TRUE(payment_task.success);

  // A task in a different project must not leak into the results.
  const TaskServiceResult other_project_task = service.CreateTask(nlohmann::json{
      {"projectId", "project_b"},
      {"title", "Fix login bug in another project"},
      {"workingDirectory", "/tmp"},
      {"tokenLimit", 1000},
  });
  ASSERT_TRUE(other_project_task.success);

  // Title match.
  const TaskServiceResult title_match =
      service.SearchTasks("project_a", "login");
  ASSERT_TRUE(title_match.success);
  ASSERT_EQ(title_match.value.size(), 1u);
  EXPECT_EQ(title_match.value[0]["title"], "Fix login bug");

  // Description match, case-insensitive.
  const TaskServiceResult description_match =
      service.SearchTasks("project_a", "CHECKOUT");
  ASSERT_TRUE(description_match.success);
  ASSERT_EQ(description_match.value.size(), 1u);
  EXPECT_EQ(description_match.value[0]["title"], "Add payment flow");

  // Every whitespace-separated keyword must match.
  const TaskServiceResult all_keywords =
      service.SearchTasks("project_a", "login bug");
  ASSERT_TRUE(all_keywords.success);
  ASSERT_EQ(all_keywords.value.size(), 1u);
  EXPECT_EQ(all_keywords.value[0]["title"], "Fix login bug");

  const TaskServiceResult missing_keyword =
      service.SearchTasks("project_a", "login payment");
  ASSERT_TRUE(missing_keyword.success);
  EXPECT_TRUE(missing_keyword.value.empty());

  // An empty keyword string returns the whole project list.
  const TaskServiceResult empty_keywords =
      service.SearchTasks("project_a", "");
  ASSERT_TRUE(empty_keywords.success);
  EXPECT_EQ(empty_keywords.value.size(), 2u);

  // The keyword filter is also available on the list endpoint.
  const TaskServiceResult listed =
      service.ListTasks("project_a", "", "", "", "login");
  ASSERT_TRUE(listed.success);
  ASSERT_EQ(listed.value.size(), 1u);
  EXPECT_EQ(listed.value[0]["title"], "Fix login bug");
}

}  // namespace
}  // namespace server
}  // namespace jiaolong