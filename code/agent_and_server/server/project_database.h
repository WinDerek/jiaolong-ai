#pragma once

#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace jiaolong {
namespace server {

// A project-declared readonly directory candidate. A task selects a subset of
// its project's catalog by id; only the selected entries become read-only
// mounts for the task. See doc/technology/multiple_folders_system_design.md.
struct ReadonlyDirectory {
  // Stable catalog id, pointed to by task selections. Empty until the project
  // is persisted, at which point one is assigned.
  std::string id;
  // Short, validated name used as the virtual root suffix
  // (`/readonly/<alias>`).
  std::string alias;
  // Absolute host directory the alias maps to.
  std::string real_path;
  // Human-readable description shown to the LLM in the system prompt.
  std::string description;
};

// A project record persisted in the Jiaolong metadata database. The field set
// mirrors the `projects` table in doc/technology/multi_projects.md.
struct Project {
  std::string id;
  std::string name;
  std::string description;
  // Default task working directory used when a task is created within the
  // project.
  std::string default_task_working_directory;
  // Projects are ordered by this value (ascending). The project with the
  // smallest ordering value is the default (current) project on the client.
  int ordering = 0;
  // The readonly-directory catalog the project's tasks select from, in
  // `ordering` order.
  std::vector<ReadonlyDirectory> readonly_directories;
};

// SQLite-backed storage for project data used by the project-management REST
// APIs. The `projects` and `project_readonly_directories` tables live in the
// same sqlite database as the tasks table (~/.jiaolong/database.sqlite by
// default). The database and its schema are expected to already exist (see
// doc/database_design.md): this class only reads and writes rows and never
// creates or migrates the schema at runtime.
class ProjectDatabase {
 public:
  // Opens the default database at ~/.jiaolong/database.sqlite. The database
  // and its schema are expected to already exist.
  ProjectDatabase();

  // Opens the database at the given path (useful for tests). The database and
  // its schema are expected to already exist.
  explicit ProjectDatabase(const std::string& db_path);

  ~ProjectDatabase();

  ProjectDatabase(const ProjectDatabase&) = delete;
  ProjectDatabase& operator=(const ProjectDatabase&) = delete;

  // Inserts a new project. When `project.id` is empty a UUID is generated.
  // The project's readonly-directory catalog is persisted as well (entries
  // without an id are assigned one). Returns the id of the created project, or
  // an empty string on failure.
  std::string CreateProject(const Project& project);

  // Returns the project with the given id (including its readonly-directory
  // catalog), or std::nullopt when not found.
  std::optional<Project> GetProject(const std::string& id);

  // Lists all projects ordered by `ordering` ascending (ties broken by id), so
  // the first entry is the project with the least ordering value. Each project
  // carries its readonly-directory catalog.
  std::vector<Project> ListProjects();

  // Updates the default task working directory of the project with the given
  // id. Called when a task is created within a project so the project keeps the
  // working directory that was used last. Returns false when no such project
  // exists.
  bool UpdateProjectDefaultTaskWorkingDirectory(
      const std::string& id, const std::string& default_task_working_directory);

  // Updates the editable fields (name, description,
  // default_task_working_directory, ordering) of the project with the given
  // id and replaces its readonly-directory catalog with
  // `readonly_directories`. Returns false when no such project exists.
  bool UpdateProject(const std::string& id, const Project& project);

  // Replaces the readonly-directory catalog of `project_id` with
  // `readonly_directories` (in order). Entries without an id are assigned a
  // UUID. Returns false on failure.
  bool ReplaceProjectReadonlyDirectories(
      const std::string& project_id,
      const std::vector<ReadonlyDirectory>& readonly_directories);

  // Returns the readonly-directory catalog of `project_id`, ordered by
  // `ordering` ascending (ties broken by id).
  std::vector<ReadonlyDirectory> ListProjectReadonlyDirectories(
      const std::string& project_id);

 private:

  void Open(const std::string& db_path);

  sqlite3* db_ = nullptr;
};

}  // namespace server
}  // namespace jiaolong