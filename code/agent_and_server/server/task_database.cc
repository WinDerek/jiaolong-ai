#include "server/task_database.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "util/uuid_generator.h"

namespace jiaolong {
namespace server {

namespace {

constexpr char kDefaultDatabaseRelativePath[] = ".jiaolong/database.sqlite";

// Returns the default database path: ~/.jiaolong/database.sqlite.
std::string DefaultDatabasePath() {
  const char* home = std::getenv("HOME");
  const std::string home_dir = home != nullptr ? home : ".";
  return (std::filesystem::path(home_dir) / kDefaultDatabaseRelativePath)
      .string();
}

// Returns the current local time formatted as "YYYY-MM-DD HH:MM:SS".
std::string NowString() {
  const std::chrono::system_clock::time_point now =
      std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm local_tm{};
#if defined(_WIN32)
  localtime_s(&local_tm, &now_time);
#else
  localtime_r(&now_time, &local_tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

// Executes a statement that returns no rows. Returns true on success.
bool Execute(sqlite3* db, const std::string& sql) {
  char* error_message = nullptr;
  const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr,
                              &error_message);
  if (rc != SQLITE_OK) {
    std::cerr << "SQLite error: "
              << (error_message != nullptr ? error_message : "unknown")
              << std::endl;
    sqlite3_free(error_message);
    return false;
  }
  return true;
}

// Returns the text value of a column, or an empty string when it is NULL.
std::string ColumnText(sqlite3_stmt* stmt, int column) {
  const unsigned char* text = sqlite3_column_text(stmt, column);
  return text != nullptr ? reinterpret_cast<const char*>(text) : "";
}

// Returns true when the given table has a column with the given name. Used to
// stay compatible with databases that have not been migrated with the manual
// `ALTER TABLE` that adds `tasks.failure_reason` yet.
bool TableHasColumn(sqlite3* db, const std::string& table_name,
    const std::string& column_name) {
  const std::string pragma_statement = "PRAGMA table_info(" + table_name + ");";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, pragma_statement.c_str(), -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return false;
  }
  bool found = false;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char* name = sqlite3_column_text(stmt, 1);
    if (name != nullptr &&
        column_name == reinterpret_cast<const char*>(name)) {
      found = true;
      break;
    }
  }
  sqlite3_finalize(stmt);
  return found;
}

// Splits a comma-separated list of ids into a vector, dropping empty entries.
// An empty string yields an empty vector.
std::vector<std::string> SplitIds(const std::string& comma_separated_ids) {
  std::vector<std::string> ids;
  std::stringstream stream(comma_separated_ids);
  std::string id;
  while (std::getline(stream, id, ',')) {
    if (!id.empty()) {
      ids.push_back(id);
    }
  }
  return ids;
}

// Joins a vector of ids into the comma-separated form stored in the
// `tasks.readonly_directories` column.
std::string JoinIds(const std::vector<std::string>& ids) {
  std::string joined;
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i > 0) {
      joined += ",";
    }
    joined += ids[i];
  }
  return joined;
}

// Splits a whitespace-separated keywords string into individual terms,
// dropping empty entries. An empty string yields an empty vector.
std::vector<std::string> SplitKeywords(const std::string& keywords) {
  std::vector<std::string> terms;
  std::stringstream stream(keywords);
  std::string term;
  while (stream >> term) {
    terms.push_back(term);
  }
  return terms;
}

// Escapes the LIKE wildcards in `value` so a user-provided keyword is matched
// literally. The returned pattern is used with `ESCAPE '\'`.
std::string EscapeLikePattern(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char c : value) {
    if (c == '\\' || c == '%' || c == '_') {
      escaped += '\\';
    }
    escaped += c;
  }
  return escaped;
}

// Converts the current row of a prepared statement (columns in the order used
// by every SELECT below) into a Task.
Task RowToTask(sqlite3_stmt* stmt) {
  Task task;
  task.id = ColumnText(stmt, 0);
  task.title = ColumnText(stmt, 1);
  task.description = ColumnText(stmt, 2);
  task.working_directory = ColumnText(stmt, 3);
  task.token_limit = sqlite3_column_int(stmt, 4);
  task.priority = ColumnText(stmt, 5);
  task.state = ColumnText(stmt, 6);
  task.current_round = sqlite3_column_int(stmt, 7);
  task.created_at = ColumnText(stmt, 8);
  task.updated_at = ColumnText(stmt, 9);
  task.agent_working = sqlite3_column_int(stmt, 10) != 0;
  task.total_token_usage = sqlite3_column_int(stmt, 11);
  task.project_id = ColumnText(stmt, 12);
  task.readonly_directories = SplitIds(ColumnText(stmt, 13));
  task.failure_reason = ColumnText(stmt, 14);
  return task;
}

// Binds a std::string to a prepared statement parameter.
void BindText(sqlite3_stmt* stmt, int index, const std::string& value) {
  sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

}  // namespace

TaskDatabase::TaskDatabase() : TaskDatabase(DefaultDatabasePath()) {}

TaskDatabase::TaskDatabase(const std::string& db_path) {
  Open(db_path);
}

TaskDatabase::~TaskDatabase() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void TaskDatabase::Open(const std::string& db_path) {
  const std::filesystem::path path(db_path);
  if (path.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
  }

  if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
    const std::string error =
        db_ != nullptr ? sqlite3_errmsg(db_) : "unknown error";
    std::cerr << "Error: Failed to open sqlite database at " << db_path
              << ": " << error << std::endl;
    if (db_ != nullptr) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    return;
  }

  // The server keeps this connection open for the whole lifetime of the
  // process. By default SQLite holds an in-memory page cache per connection,
  // so pages read before another process (e.g. the sqlite3 shell) modifies the
  // database can keep serving stale task data (for example agent_working = 1
  // even after it was manually set to 0). Disable the page cache so every read
  // goes to disk and always reflects the latest committed state.
  Execute(db_, "PRAGMA cache_size = 0;");

  // Detect whether the manual `failure_reason` column upgrade has been applied
  // so the read/write statements below can stay compatible with older
  // databases that have not been migrated yet.
  has_failure_reason_ = TableHasColumn(db_, "tasks", "failure_reason");
}

std::string TaskDatabase::CreateTask(const Task& task) {
  if (db_ == nullptr) {
    return "";
  }
  Task new_task = task;
  if (new_task.id.empty()) {
    new_task.id = GenerateUuid();
  }
  if (new_task.priority.empty()) {
    new_task.priority = "normal";
  }
  if (new_task.state.empty()) {
    new_task.state = "todo";
  }
  const std::string now = NowString();
  new_task.created_at = now;
  new_task.updated_at = now;

  sqlite3_stmt* stmt = nullptr;
  std::string insert_task =
      "INSERT INTO tasks (id, title, description, working_directory,"
      "  token_limit, priority, state, current_round, agent_working,"
      "  created_at, updated_at, total_token_usage, project_id,"
      "  readonly_directories";
  if (has_failure_reason_) {
    insert_task += ", failure_reason";
  }
  insert_task += ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?";
  if (has_failure_reason_) {
    insert_task += ", ?";
  }
  insert_task += ");";
  if (sqlite3_prepare_v2(db_, insert_task.c_str(), -1, &stmt, nullptr) !=
      SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return "";
  }

  BindText(stmt, 1, new_task.id);
  BindText(stmt, 2, new_task.title);
  BindText(stmt, 3, new_task.description);
  BindText(stmt, 4, new_task.working_directory);
  sqlite3_bind_int(stmt, 5, new_task.token_limit);
  BindText(stmt, 6, new_task.priority);
  BindText(stmt, 7, new_task.state);
  sqlite3_bind_int(stmt, 8, new_task.current_round);
  sqlite3_bind_int(stmt, 9, new_task.agent_working ? 1 : 0);
  BindText(stmt, 10, new_task.created_at);
  BindText(stmt, 11, new_task.updated_at);
  sqlite3_bind_int(stmt, 12, new_task.total_token_usage);
  BindText(stmt, 13, new_task.project_id);
  BindText(stmt, 14, JoinIds(new_task.readonly_directories));
  if (has_failure_reason_) {
    // `failure_reason` is not user-editable, so a task created through the
    // REST API always starts with no recorded failure (the field defaults to
    // an empty string).
    BindText(stmt, 15, new_task.failure_reason);
  }

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return "";
  }
  return new_task.id;
}

bool TaskDatabase::UpdateTask(const std::string& id, const Task& task) {
  if (db_ == nullptr) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  static const char kUpdateTask[] =
      "UPDATE tasks SET title = ?, description = ?, working_directory = ?,"
      "  token_limit = ?, priority = ?, state = ?,"
      "  readonly_directories = ?, updated_at = ? WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, kUpdateTask, -1, &stmt, nullptr) != SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }

  BindText(stmt, 1, task.title);
  BindText(stmt, 2, task.description);
  BindText(stmt, 3, task.working_directory);
  sqlite3_bind_int(stmt, 4, task.token_limit);
  BindText(stmt, 5, task.priority);
  BindText(stmt, 6, task.state);
  BindText(stmt, 7, JoinIds(task.readonly_directories));
  BindText(stmt, 8, NowString());
  BindText(stmt, 9, id);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }
  return sqlite3_changes(db_) > 0;
}

std::optional<Task> TaskDatabase::GetTask(const std::string& id) {
  if (db_ == nullptr) {
    return std::nullopt;
  }
  sqlite3_stmt* stmt = nullptr;
  std::string select_task =
      "SELECT id, title, description, working_directory, token_limit,"
      "  priority, state, current_round, created_at, updated_at, agent_working,"
      "  total_token_usage, project_id, readonly_directories";
  select_task += has_failure_reason_ ? ", failure_reason" : ", ''";
  select_task += " FROM tasks WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, select_task.c_str(), -1, &stmt, nullptr) !=
      SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return std::nullopt;
  }

  BindText(stmt, 1, id);
  std::optional<Task> result = std::nullopt;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = RowToTask(stmt);
  }
  sqlite3_finalize(stmt);
  return result;
}

std::vector<Task> TaskDatabase::ListTasks(const std::string& project_id,
                                          const std::string& state_filter,
                                          const std::string& sort_by,
                                          const std::string& order,
                                          const std::string& keywords) {
  if (db_ == nullptr) {
    return {};
  }
  std::string sql =
      "SELECT id, title, description, working_directory, token_limit,"
      "  priority, state, current_round, created_at, updated_at, agent_working,"
      "  total_token_usage, project_id, readonly_directories";
  sql += has_failure_reason_ ? ", failure_reason" : ", ''";
  sql += " FROM tasks WHERE project_id = ?";
  if (!state_filter.empty()) {
    sql += " AND state = ?";
  }

  // Keyword search: every whitespace-separated term must appear in the title
  // or the description (case-insensitive). Wildcards are escaped so a keyword
  // is matched literally.
  const std::vector<std::string> keyword_terms = SplitKeywords(keywords);
  for (size_t i = 0; i < keyword_terms.size(); ++i) {
    sql += " AND (title LIKE ? ESCAPE '\\'"
           " OR description LIKE ? ESCAPE '\\')";
  }

  // Only whitelisted sort columns and directions are accepted; priority is
  // ranked low < normal < high instead of alphabetically. The default (used
  // when no sort is requested, e.g. by the dashboard) sorts by task state:
  // needs_review first, in_progress second, todo third, failed fourth, and
  // completed (done/approved) last. id is used as a deterministic
  // tie-breaker within the same state.
  std::string sort_column =
      "CASE state WHEN 'needs_review' THEN 0 WHEN 'in_progress' THEN 1"
      " WHEN 'todo' THEN 2 WHEN 'failed' THEN 3"
      " WHEN 'completed' THEN 4 WHEN 'done' THEN 4 WHEN 'approved' THEN 4"
      " ELSE 5 END";
  if (sort_by == "id") {
    sort_column = "id";
  } else if (sort_by == "priority") {
    sort_column =
        "CASE priority WHEN 'low' THEN 0 WHEN 'normal' THEN 1"
        " WHEN 'high' THEN 2 ELSE 1 END";
  }
  std::string sort_direction = "ASC";
  if (order == "desc") {
    sort_direction = "DESC";
  }
  sql += " ORDER BY " + sort_column + " " + sort_direction;
  if (sort_by != "id" && sort_by != "priority") {
    // Keep the status ordering stable by falling back to id for tasks that
    // share the same state.
    sql += ", id ASC";
  }

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return {};
  }
  int parameter_index = 1;
  BindText(stmt, parameter_index++, project_id);
  if (!state_filter.empty()) {
    BindText(stmt, parameter_index++, state_filter);
  }
  for (const std::string& term : keyword_terms) {
    const std::string pattern = "%" + EscapeLikePattern(term) + "%";
    BindText(stmt, parameter_index++, pattern);
    BindText(stmt, parameter_index++, pattern);
  }

  std::vector<Task> tasks;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    tasks.push_back(RowToTask(stmt));
  }
  sqlite3_finalize(stmt);
  return tasks;
}

std::vector<Task> TaskDatabase::SearchTasks(const std::string& project_id,
                                            const std::string& keywords) {
  return ListTasks(project_id, "", "", "asc", keywords);
}

int TaskDatabase::GetTotalTokenUsage() const {
  if (db_ == nullptr) {
    return 0;
  }
  sqlite3_stmt* stmt = nullptr;
  static const char kTotalTokenUsage[] =
      "SELECT COALESCE(SUM(total_token_usage), 0) FROM tasks;";
  if (sqlite3_prepare_v2(db_, kTotalTokenUsage, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return 0;
  }
  int total_token_usage = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    total_token_usage = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return total_token_usage;
}

bool TaskDatabase::DeleteTask(const std::string& id) {
  if (db_ == nullptr) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  static const char kDeleteTask[] = "DELETE FROM tasks WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, kDeleteTask, -1, &stmt, nullptr) != SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }

  BindText(stmt, 1, id);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }
  return sqlite3_changes(db_) > 0;
}

bool TaskDatabase::StartWork(const std::string& id) {
  if (db_ == nullptr) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  std::string start_work =
      "UPDATE tasks SET state = 'in_progress', agent_working = 1,"
      "  current_round = current_round + 1";
  if (has_failure_reason_) {
    // A resumed task starts a fresh round, so any stale reason is cleared.
    start_work += ", failure_reason = ''";
  }
  start_work += ", updated_at = ? WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, start_work.c_str(), -1, &stmt, nullptr) !=
      SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }

  BindText(stmt, 1, NowString());
  BindText(stmt, 2, id);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }
  return sqlite3_changes(db_) > 0;
}

bool TaskDatabase::CompleteWork(const std::string& id) {
  if (db_ == nullptr) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  std::string complete_work =
      "UPDATE tasks SET state = 'needs_review', agent_working = 0";
  if (has_failure_reason_) {
    // Clear the reason defensively on success.
    complete_work += ", failure_reason = ''";
  }
  complete_work += ", updated_at = ? WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, complete_work.c_str(), -1, &stmt, nullptr) !=
      SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }

  BindText(stmt, 1, NowString());
  BindText(stmt, 2, id);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }
  return sqlite3_changes(db_) > 0;
}

bool TaskDatabase::FailWork(const std::string& id) {
  if (db_ == nullptr) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  std::string fail_work =
      "UPDATE tasks SET state = 'failed', agent_working = 0";
  if (has_failure_reason_) {
    // Preserve the reason the CLI recorded; fall back to a generic execution
    // failure when the CLI crashed or was killed before it could write one, so
    // the UI always has something to show.
    fail_work +=
        ", failure_reason = CASE WHEN failure_reason = ''"
        " THEN 'execution_failure' ELSE failure_reason END";
  }
  fail_work += ", updated_at = ? WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, fail_work.c_str(), -1, &stmt, nullptr) !=
      SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }

  BindText(stmt, 1, NowString());
  BindText(stmt, 2, id);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }
  return sqlite3_changes(db_) > 0;
}

bool TaskDatabase::ApproveTask(const std::string& id) {
  if (db_ == nullptr) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  static const char kApproveTask[] =
      "UPDATE tasks SET state = 'done', agent_working = 0, updated_at = ?"
      "  WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, kApproveTask, -1, &stmt, nullptr) != SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }

  BindText(stmt, 1, NowString());
  BindText(stmt, 2, id);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }
  return sqlite3_changes(db_) > 0;
}

bool TaskDatabase::ConfirmCompletion(const std::string& id) {
  if (db_ == nullptr) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  static const char kConfirmCompletion[] =
      "UPDATE tasks SET state = 'completed', agent_working = 0, updated_at = ?"
      "  WHERE id = ? AND state = 'needs_review';";
  if (sqlite3_prepare_v2(db_, kConfirmCompletion, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }

  BindText(stmt, 1, NowString());
  BindText(stmt, 2, id);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }
  return sqlite3_changes(db_) > 0;
}

}  // namespace server
}  // namespace jiaolong
