#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "server/project_database.h"

namespace jiaolong {
namespace server {

// Kinds of failures a ProjectService operation can report. Each maps to an
// HTTP status code in the REST layer.
enum class ProjectServiceErrorType {
  kNone,
  // The referenced project does not exist (HTTP 404).
  kNotFound,
  // The request body is missing required fields or contains invalid values
  // (HTTP 400).
  kInvalidInput,
  // Unexpected internal failure (HTTP 500).
  kInternal,
};

// Result of a ProjectService operation. When `success` is false, `error_type`
// and `error_message` describe the failure.
struct ProjectServiceResult {
  bool success = false;
  nlohmann::json value;
  ProjectServiceErrorType error_type = ProjectServiceErrorType::kInternal;
  std::string error_message;
};

// Business logic for the project-management REST APIs. ProjectService
// translates request payloads into ProjectDatabase operations and serializes
// the results back to JSON. It does not own any storage; persistence is
// delegated to the ProjectDatabase it is constructed with.
class ProjectService {
 public:
  explicit ProjectService(ProjectDatabase& database);

  // GET /api/projects
  // Lists all projects as a JSON array ordered by `ordering` ascending (the
  // project with the least ordering value first).
  ProjectServiceResult ListProjects() const;

  // POST /api/projects
  // Creates a project from the JSON request body and returns the created
  // project as JSON. `name` and `defaultTaskWorkingDirectory` are mandatory;
  // `description` (default "") and `ordering` (default 0) are optional.
  ProjectServiceResult CreateProject(const nlohmann::json& request) const;

  // GET /api/projects/{id}
  // Returns the project with the given id (including its readonly-directory
  // catalog) as JSON. Reports kNotFound when no such project exists.
  ProjectServiceResult GetProject(const std::string& id) const;

  // POST /api/projects/{id}/readonly-directories
  // Appends a new readonly directory to the project's catalog from the JSON
  // request body and returns the updated project as JSON. `alias` and
  // `realPath` are mandatory; `description` (default "") is optional. Reports
  // kNotFound when the project does not exist and kInvalidInput when the entry
  // is invalid or an entry with the same alias already exists.
  ProjectServiceResult AddReadonlyDirectory(const std::string& id,
      const nlohmann::json& request) const;

  // PUT /api/projects/{id}
  // Updates an existing project from the JSON request body and returns the
  // updated project as JSON. Fields absent from the body keep their current
  // value; `readonlyDirectories`, when present, replaces the project's
  // readonly-directory catalog. Reports kNotFound when no such project exists.
  ProjectServiceResult UpdateProject(const std::string& id,
      const nlohmann::json& request) const;

 private:
  ProjectDatabase& database_;
};

}  // namespace server
}  // namespace jiaolong
