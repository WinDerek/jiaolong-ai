#pragma once

#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace jiaolong {
namespace server {

// A task record persisted in the Jiaolong metadata database. The field set
// mirrors the Task data model in
// doc/technology/remote_working_system_design.md.
struct Task {
  std::string id;
  // Id of the project this task belongs to. A task is always created under a
  // project.
  std::string project_id;
  std::string title;
  std::string description;
  std::string working_directory;
  int token_limit = 0;
  // One of: low | normal | high.
  std::string priority;
  // One of: todo | in_progress | needs_review | failed | completed.
  std::string state;
  int current_round = 0;
  // Whether the Jiaolong agent is currently working on the task.
  bool agent_working = false;
  // Total number of tokens the agent consumed while working on the task,
  // persisted after a successful run so it can be reviewed later.
  int total_token_usage = 0;
  std::string created_at;
  std::string updated_at;
  // Ids of the project's readonly directories this task selected, in mount
  // order. Empty by default (the task mounts only `/workspace`). Persisted as
  // the comma-separated `tasks.readonly_directories` column.
  std::vector<std::string> readonly_directories;
  // Why the task is in the failed state. Empty when there is no failure.
  // One of: "" | "token_not_enough" | "execution_failure". Persisted as the
  // `tasks.failure_reason` column.
  std::string failure_reason;
};

// SQLite-backed storage for task data used by the task-management REST APIs.
//
// By default the database lives at ~/.jiaolong/database.sqlite. The database
// and its schema are expected to already exist (see doc/database_design.md):
// this class only reads and writes rows and never creates or migrates the
// schema at runtime. The schema must be applied manually (for example with the
// sqlite3 CLI) before the server starts against a new or updated database.
class TaskDatabase {
 public:
  // Opens the default database at ~/.jiaolong/database.sqlite. The database
  // and its schema are expected to already exist.
  TaskDatabase();

  // Opens the database at the given path (useful for tests). The database and
  // its schema are expected to already exist.
  explicit TaskDatabase(const std::string& db_path);

  ~TaskDatabase();

  TaskDatabase(const TaskDatabase&) = delete;
  TaskDatabase& operator=(const TaskDatabase&) = delete;

  // Inserts a new task under the project referenced by `task.project_id`.
  // When `task.id` is empty a UUID is generated. Returns the id of the
  // created task, or an empty string on failure.
  std::string CreateTask(const Task& task);

  // Updates the editable fields (title, description, working_directory,
  // token_limit, priority, state) of the task with the given id. Returns
  // false when no such task exists.
  bool UpdateTask(const std::string& id, const Task& task);

  // Deletes the task with the given id. Returns false when no such task
  // exists.
  bool DeleteTask(const std::string& id);

  // Returns the task with the given id, or std::nullopt when not found.
  std::optional<Task> GetTask(const std::string& id);

  // Lists the tasks that belong to the given project. `project_id` is
  // mandatory and only tasks whose project id matches are returned. When
  // `state_filter` is non-empty, only tasks whose state matches are returned.
  // `sort_by` is "id", "priority" or "status" and `order` is "asc" or "desc".
  // Any other `sort_by` (e.g. the empty default used by the dashboard) falls
  // back to status ordering: needs_review, in_progress, todo, failed,
  // completed (done/approved) last, with id as tie-breaker. When `keywords` is
  // non-empty it is split on whitespace and only tasks whose title or
  // description contains every keyword (case-insensitive) are returned.
  std::vector<Task> ListTasks(const std::string& project_id,
                              const std::string& state_filter = "",
                              const std::string& sort_by = "",
                              const std::string& order = "asc",
                              const std::string& keywords = "");

  // Searches the tasks of the given project by keywords. `keywords` is split
  // on whitespace and a task matches when its title or description contains
  // every keyword (case-insensitive). An empty `keywords` string matches every
  // task of the project. Results use the same default status ordering as
  // ListTasks.
  std::vector<Task> SearchTasks(const std::string& project_id,
                                const std::string& keywords);

  // Returns the sum of the total_token_usage of all tasks, or 0 when there
  // are no tasks.
  int GetTotalTokenUsage() const;

  // Marks a task as in_progress, sets agent_working to true and increments its
  // current_round. Returns false when no such task exists.
  bool StartWork(const std::string& id);

  // Marks a task as no longer being worked on by the agent: clears
  // agent_working and moves the task to needs_review (the agent finished
  // working and the LLM stopped naturally). Returns false when no such task
  // exists.
  bool CompleteWork(const std::string& id);

  // Marks a task as no longer being worked on by the agent: clears
  // agent_working and moves the task to failed (the agent finished working
  // but the LLM did not stop naturally, e.g. the response hit the token
  // limit or an LLM API error occurred). Returns false when no such task
  // exists.
  bool FailWork(const std::string& id);

  // Marks a task as done and clears agent_working (the approve flow). Returns
  // false when no such task exists.
  bool ApproveTask(const std::string& id);

  // Marks a task as completed. The transition is only legal when the task is
  // currently in the needs_review state; this is enforced with a state guard
  // in the UPDATE statement. Returns false when no such task exists or when
  // the task is not in the needs_review state.
  bool ConfirmCompletion(const std::string& id);

 private:
  void Open(const std::string& db_path);

  sqlite3* db_ = nullptr;

  // Whether the `tasks` table has the `failure_reason` column. The column is
  // added by a manual schema upgrade (see
  // doc/technology/task_failure_reason_system_design.md) and neither the server
  // nor the CLI migrates the schema at runtime. When an older database has not
  // been migrated yet every query falls back to the pre-feature column set so
  // the server keeps working unchanged.
  bool has_failure_reason_ = false;
};

}  // namespace server
}  // namespace jiaolong
