#include "server/project_database.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
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

// Converts the current row of a prepared statement (columns in the order used
// by every SELECT below) into a Project.
Project RowToProject(sqlite3_stmt* stmt) {
  Project project;
  project.id = ColumnText(stmt, 0);
  project.name = ColumnText(stmt, 1);
  project.description = ColumnText(stmt, 2);
  project.default_task_working_directory = ColumnText(stmt, 3);
  project.ordering = sqlite3_column_int(stmt, 4);
  return project;
}

// Converts the current row of a prepared statement (columns in the order used
// by the readonly-directory SELECT) into a ReadonlyDirectory.
ReadonlyDirectory RowToReadonlyDirectory(sqlite3_stmt* stmt) {
  ReadonlyDirectory directory;
  directory.id = ColumnText(stmt, 0);
  directory.alias = ColumnText(stmt, 1);
  directory.real_path = ColumnText(stmt, 2);
  directory.description = ColumnText(stmt, 3);
  return directory;
}

// Binds a std::string to a prepared statement parameter.
void BindText(sqlite3_stmt* stmt, int index, const std::string& value) {
  sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

}  // namespace

ProjectDatabase::ProjectDatabase() : ProjectDatabase(DefaultDatabasePath()) {}

ProjectDatabase::ProjectDatabase(const std::string& db_path) {
  Open(db_path);
}

ProjectDatabase::~ProjectDatabase() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void ProjectDatabase::Open(const std::string& db_path) {
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

  // Keep reads consistent with the tasks table connection: disable the page
  // cache so every read goes to disk and always reflects the latest committed
  // state (see TaskDatabase::Open for the rationale).
  Execute(db_, "PRAGMA cache_size = 0;");
}

std::string ProjectDatabase::CreateProject(const Project& project) {
  if (db_ == nullptr) {
    return "";
  }
  Project new_project = project;
  if (new_project.id.empty()) {
    new_project.id = GenerateUuid();
  }

  sqlite3_stmt* stmt = nullptr;
  static const char kInsertProject[] =
      "INSERT INTO projects (id, name, description,"
      "  default_task_working_directory, ordering)"
      " VALUES (?, ?, ?, ?, ?);";
  if (sqlite3_prepare_v2(db_, kInsertProject, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return "";
  }

  BindText(stmt, 1, new_project.id);
  BindText(stmt, 2, new_project.name);
  BindText(stmt, 3, new_project.description);
  BindText(stmt, 4, new_project.default_task_working_directory);
  sqlite3_bind_int(stmt, 5, new_project.ordering);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return "";
  }
  ReplaceProjectReadonlyDirectories(new_project.id,
      new_project.readonly_directories);
  return new_project.id;
}

std::optional<Project> ProjectDatabase::GetProject(const std::string& id) {
  if (db_ == nullptr) {
    return std::nullopt;
  }
  sqlite3_stmt* stmt = nullptr;
  static const char kSelectProject[] =
      "SELECT id, name, description, default_task_working_directory, ordering"
      "  FROM projects WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, kSelectProject, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return std::nullopt;
  }

  BindText(stmt, 1, id);
  std::optional<Project> result = std::nullopt;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = RowToProject(stmt);
  }
  sqlite3_finalize(stmt);
  if (result.has_value()) {
    result->readonly_directories = ListProjectReadonlyDirectories(result->id);
  }
  return result;
}

std::vector<Project> ProjectDatabase::ListProjects() {
  if (db_ == nullptr) {
    return {};
  }
  static const char kSelectProjects[] =
      "SELECT id, name, description, default_task_working_directory, ordering"
      "  FROM projects ORDER BY ordering ASC, id ASC;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, kSelectProjects, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return {};
  }

  std::vector<Project> projects;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    projects.push_back(RowToProject(stmt));
  }
  sqlite3_finalize(stmt);
  for (Project& project : projects) {
    project.readonly_directories =
        ListProjectReadonlyDirectories(project.id);
  }
  return projects;
}

bool ProjectDatabase::UpdateProjectDefaultTaskWorkingDirectory(
    const std::string& id,
    const std::string& default_task_working_directory) {
  if (db_ == nullptr) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  static const char kUpdateProject[] =
      "UPDATE projects SET default_task_working_directory = ? WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, kUpdateProject, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }

  BindText(stmt, 1, default_task_working_directory);
  BindText(stmt, 2, id);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }
  return sqlite3_changes(db_) > 0;
}

bool ProjectDatabase::UpdateProject(const std::string& id,
    const Project& project) {
  if (db_ == nullptr) {
    return false;
  }
  if (!GetProject(id).has_value()) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  static const char kUpdateProject[] =
      "UPDATE projects SET name = ?, description = ?,"
      "  default_task_working_directory = ?, ordering = ? WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, kUpdateProject, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }
  BindText(stmt, 1, project.name);
  BindText(stmt, 2, project.description);
  BindText(stmt, 3, project.default_task_working_directory);
  sqlite3_bind_int(stmt, 4, project.ordering);
  BindText(stmt, 5, id);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }
  ReplaceProjectReadonlyDirectories(id, project.readonly_directories);
  return true;
}

bool ProjectDatabase::ReplaceProjectReadonlyDirectories(
    const std::string& project_id,
    const std::vector<ReadonlyDirectory>& readonly_directories) {
  if (db_ == nullptr) {
    return false;
  }

  sqlite3_stmt* delete_stmt = nullptr;
  static const char kDeleteReadonlyDirectories[] =
      "DELETE FROM project_readonly_directories WHERE project_id = ?;";
  if (sqlite3_prepare_v2(db_, kDeleteReadonlyDirectories, -1, &delete_stmt,
          nullptr) != SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }
  BindText(delete_stmt, 1, project_id);
  const int delete_rc = sqlite3_step(delete_stmt);
  sqlite3_finalize(delete_stmt);
  if (delete_rc != SQLITE_DONE) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }

  int ordering = 0;
  for (const ReadonlyDirectory& directory : readonly_directories) {
    ReadonlyDirectory entry = directory;
    if (entry.id.empty()) {
      entry.id = GenerateUuid();
    }
    sqlite3_stmt* stmt = nullptr;
    static const char kInsertReadonlyDirectory[] =
        "INSERT INTO project_readonly_directories"
        "  (id, project_id, alias, real_path, description, ordering)"
        "  VALUES (?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, kInsertReadonlyDirectory, -1, &stmt, nullptr) !=
        SQLITE_OK) {
      std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
      return false;
    }
    BindText(stmt, 1, entry.id);
    BindText(stmt, 2, project_id);
    BindText(stmt, 3, entry.alias);
    BindText(stmt, 4, entry.real_path);
    BindText(stmt, 5, entry.description);
    sqlite3_bind_int(stmt, 6, ordering);
    const int insert_rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (insert_rc != SQLITE_DONE) {
      std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
      return false;
    }
    ++ordering;
  }
  return true;
}

std::vector<ReadonlyDirectory>
ProjectDatabase::ListProjectReadonlyDirectories(const std::string& project_id) {
  if (db_ == nullptr) {
    return {};
  }
  static const char kSelectReadonlyDirectories[] =
      "SELECT id, alias, real_path, description"
      "  FROM project_readonly_directories WHERE project_id = ?"
      "  ORDER BY ordering ASC, id ASC;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, kSelectReadonlyDirectories, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    std::cerr << "SQLite error: " << sqlite3_errmsg(db_) << std::endl;
    return {};
  }
  BindText(stmt, 1, project_id);
  std::vector<ReadonlyDirectory> directories;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    directories.push_back(RowToReadonlyDirectory(stmt));
  }
  sqlite3_finalize(stmt);
  return directories;
}

}  // namespace server
}  // namespace jiaolong
