#include "server/project_service.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace jiaolong {
namespace server {

namespace {

constexpr char kId[] = "id";
constexpr char kName[] = "name";
constexpr char kDescription[] = "description";
constexpr char kDefaultTaskWorkingDirectory[] = "defaultTaskWorkingDirectory";
constexpr char kOrdering[] = "ordering";
constexpr char kReadonlyDirectories[] = "readonlyDirectories";

ProjectServiceResult Failure(ProjectServiceErrorType type,
                             const std::string& message) {
  ProjectServiceResult result;
  result.success = false;
  result.error_type = type;
  result.error_message = message;
  return result;
}

ProjectServiceResult Success(nlohmann::json value) {
  ProjectServiceResult result;
  result.success = true;
  result.value = std::move(value);
  return result;
}

std::optional<std::string> RequireString(const nlohmann::json& json,
                                         const char* key);
std::string OptionalString(const nlohmann::json& json, const char* key,
                           const std::string& fallback);

nlohmann::json ReadonlyDirectoryToJson(const ReadonlyDirectory& directory) {
  return nlohmann::json{
      {kId, directory.id},
      {"alias", directory.alias},
      {"realPath", directory.real_path},
      {kDescription, directory.description},
  };
}

nlohmann::json ProjectToJson(const Project& project) {
  nlohmann::json readonly_directories = nlohmann::json::array();
  for (const ReadonlyDirectory& directory : project.readonly_directories) {
    readonly_directories.push_back(ReadonlyDirectoryToJson(directory));
  }
  return nlohmann::json{
      {kId, project.id},
      {kName, project.name},
      {kDescription, project.description},
      {kDefaultTaskWorkingDirectory, project.default_task_working_directory},
      {kOrdering, project.ordering},
      {kReadonlyDirectories, readonly_directories},
  };
}

// Returns true when `alias` is a valid readonly-directory alias: non-empty and
// matching ^[A-Za-z0-9_-]+$, and not the reserved word "workspace".
bool IsValidAlias(const std::string& alias) {
  if (alias.empty() || alias == "workspace") {
    return false;
  }
  for (const char c : alias) {
    const bool is_letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    const bool is_digit = c >= '0' && c <= '9';
    if (!is_letter && !is_digit && c != '_' && c != '-') {
      return false;
    }
  }
  return true;
}

// Parses the optional `readonlyDirectories` field of a project request into
// `readonly_directories`. Returns an empty string on success, or a
// human-readable error message on failure.
std::string ParseReadonlyDirectories(const nlohmann::json& request,
                                     std::vector<ReadonlyDirectory>* out) {
  if (!request.contains(kReadonlyDirectories)) {
    return "";
  }
  const nlohmann::json& catalog_json = request[kReadonlyDirectories];
  if (!catalog_json.is_array()) {
    return "field readonlyDirectories must be an array";
  }
  std::vector<ReadonlyDirectory> directories;
  std::vector<std::string> aliases;
  for (const nlohmann::json& entry_json : catalog_json) {
    if (!entry_json.is_object()) {
      return "each readonlyDirectories entry must be an object";
    }
    ReadonlyDirectory directory;
    const std::optional<std::string> alias =
        RequireString(entry_json, "alias");
    const std::optional<std::string> real_path =
        RequireString(entry_json, "realPath");
    if (!alias.has_value() || !IsValidAlias(*alias)) {
      return "readonlyDirectories alias must match ^[A-Za-z0-9_-]+$ and must "
             "not be \"workspace\"";
    }
    if (std::find(aliases.begin(), aliases.end(), *alias) != aliases.end()) {
      return "readonlyDirectories aliases must be unique";
    }
    aliases.push_back(*alias);
    if (!real_path.has_value() || real_path->empty()) {
      return "readonlyDirectories realPath must be a non-empty string";
    }
    const std::filesystem::path real_path_fs(*real_path);
    if (!real_path_fs.is_absolute()) {
      return "readonlyDirectories realPath must be an absolute path";
    }
    directory.alias = *alias;
    directory.real_path = *real_path;
    directory.description = OptionalString(entry_json, kDescription, "");
    if (entry_json.contains(kId) && entry_json[kId].is_string()) {
      directory.id = entry_json[kId].get<std::string>();
    }
    directories.push_back(std::move(directory));
  }
  *out = std::move(directories);
  return "";
}

// Parses a single readonly-directory object into `out`. Returns an empty
// string on success, or a human-readable error message on failure.
std::string ParseReadonlyDirectory(const nlohmann::json& entry_json,
                                   ReadonlyDirectory* out) {
  if (!entry_json.is_object()) {
    return "readonly directory must be a JSON object";
  }
  const std::optional<std::string> alias = RequireString(entry_json, "alias");
  const std::optional<std::string> real_path =
      RequireString(entry_json, "realPath");
  if (!alias.has_value() || !IsValidAlias(*alias)) {
    return "readonlyDirectories alias must match ^[A-Za-z0-9_-]+$ and must "
           "not be \"workspace\"";
  }
  if (!real_path.has_value() || real_path->empty()) {
    return "readonlyDirectories realPath must be a non-empty string";
  }
  const std::filesystem::path real_path_fs(*real_path);
  if (!real_path_fs.is_absolute()) {
    return "readonlyDirectories realPath must be an absolute path";
  }
  ReadonlyDirectory directory;
  directory.alias = *alias;
  directory.real_path = *real_path;
  directory.description = OptionalString(entry_json, kDescription, "");
  if (entry_json.contains(kId) && entry_json[kId].is_string()) {
    directory.id = entry_json[kId].get<std::string>();
  }
  *out = std::move(directory);
  return "";
}

// Returns the value of a required string field, or std::nullopt when the
// field is missing or not a string.
std::optional<std::string> RequireString(const nlohmann::json& json,
                                         const char* key) {
  const auto it = json.find(key);
  if (it == json.end() || !it->is_string()) {
    return std::nullopt;
  }
  return it->get<std::string>();
}

// Returns the value of an optional string field, or `fallback` when the field
// is missing or null.
std::string OptionalString(const nlohmann::json& json, const char* key,
                           const std::string& fallback) {
  const auto it = json.find(key);
  if (it == json.end() || it->is_null() || !it->is_string()) {
    return fallback;
  }
  return it->get<std::string>();
}

// Returns the value of an optional integer field, or `fallback` when the
// field is missing, null, or not an integer.
int OptionalInt(const nlohmann::json& json, const char* key, int fallback) {
  const auto it = json.find(key);
  if (it == json.end() || it->is_null() || !it->is_number_integer()) {
    return fallback;
  }
  return it->get<int>();
}

}  // namespace

ProjectService::ProjectService(ProjectDatabase& database)
    : database_(database) {}

ProjectServiceResult ProjectService::ListProjects() const {
  const std::vector<Project> projects = database_.ListProjects();
  nlohmann::json result = nlohmann::json::array();
  for (const Project& project : projects) {
    result.push_back(ProjectToJson(project));
  }
  return Success(result);
}

ProjectServiceResult ProjectService::CreateProject(
    const nlohmann::json& request) const {
  if (!request.is_object()) {
    return Failure(ProjectServiceErrorType::kInvalidInput,
                   "request body must be a JSON object");
  }

  Project project;
  const std::optional<std::string> name = RequireString(request, kName);
  if (!name.has_value() || name->empty()) {
    return Failure(ProjectServiceErrorType::kInvalidInput,
                   "missing or invalid field: name");
  }
  project.name = *name;

  project.description = OptionalString(request, kDescription, "");

  const std::optional<std::string> default_task_working_directory =
      RequireString(request, kDefaultTaskWorkingDirectory);
  if (!default_task_working_directory.has_value() ||
      default_task_working_directory->empty()) {
    return Failure(ProjectServiceErrorType::kInvalidInput,
                   "missing or invalid field: defaultTaskWorkingDirectory");
  }
  project.default_task_working_directory = *default_task_working_directory;

  project.ordering = OptionalInt(request, kOrdering, 0);

  const std::string readonly_directories_error =
      ParseReadonlyDirectories(request, &project.readonly_directories);
  if (!readonly_directories_error.empty()) {
    return Failure(ProjectServiceErrorType::kInvalidInput,
                   readonly_directories_error);
  }

  const std::string id = database_.CreateProject(project);
  if (id.empty()) {
    return Failure(ProjectServiceErrorType::kInternal,
                   "failed to create project");
  }
  const std::optional<Project> created = database_.GetProject(id);
  if (!created.has_value()) {
    return Failure(ProjectServiceErrorType::kInternal,
                   "failed to read back the created project");
  }
  return Success(ProjectToJson(*created));
}

ProjectServiceResult ProjectService::GetProject(const std::string& id) const {
  const std::optional<Project> project = database_.GetProject(id);
  if (!project.has_value()) {
    return Failure(ProjectServiceErrorType::kNotFound,
                   "project not found: " + id);
  }
  return Success(ProjectToJson(*project));
}

ProjectServiceResult ProjectService::AddReadonlyDirectory(
    const std::string& id, const nlohmann::json& request) const {
  if (!request.is_object()) {
    return Failure(ProjectServiceErrorType::kInvalidInput,
                   "request body must be a JSON object");
  }
  const std::optional<Project> existing = database_.GetProject(id);
  if (!existing.has_value()) {
    return Failure(ProjectServiceErrorType::kNotFound,
                   "project not found: " + id);
  }

  ReadonlyDirectory directory;
  const std::string parse_error = ParseReadonlyDirectory(request, &directory);
  if (!parse_error.empty()) {
    return Failure(ProjectServiceErrorType::kInvalidInput, parse_error);
  }
  for (const ReadonlyDirectory& candidate : existing->readonly_directories) {
    if (candidate.alias == directory.alias) {
      return Failure(ProjectServiceErrorType::kInvalidInput,
                     "readonlyDirectories aliases must be unique");
    }
  }

  // Append to the existing catalog and persist the whole catalog. Passing the
  // existing entries along preserves their ids (the new entry gets one
  // assigned by the database layer).
  std::vector<ReadonlyDirectory> readonly_directories =
      existing->readonly_directories;
  readonly_directories.push_back(std::move(directory));
  if (!database_.ReplaceProjectReadonlyDirectories(
          id, readonly_directories)) {
    return Failure(ProjectServiceErrorType::kInternal,
                   "failed to add readonly directory");
  }
  const std::optional<Project> updated = database_.GetProject(id);
  if (!updated.has_value()) {
    return Failure(ProjectServiceErrorType::kInternal,
                   "failed to read back the updated project");
  }
  return Success(ProjectToJson(*updated));
}

ProjectServiceResult ProjectService::UpdateProject(
    const std::string& id, const nlohmann::json& request) const {
  if (!request.is_object()) {
    return Failure(ProjectServiceErrorType::kInvalidInput,
                   "request body must be a JSON object");
  }
  const std::optional<Project> existing = database_.GetProject(id);
  if (!existing.has_value()) {
    return Failure(ProjectServiceErrorType::kNotFound,
                   "project not found: " + id);
  }

  Project project = *existing;
  if (request.contains(kName)) {
    const std::optional<std::string> name = RequireString(request, kName);
    if (!name.has_value() || name->empty()) {
      return Failure(ProjectServiceErrorType::kInvalidInput,
                     "missing or invalid field: name");
    }
    project.name = *name;
  }
  if (request.contains(kDescription)) {
    project.description = OptionalString(request, kDescription, "");
  }
  if (request.contains(kDefaultTaskWorkingDirectory)) {
    const std::optional<std::string> default_task_working_directory =
        RequireString(request, kDefaultTaskWorkingDirectory);
    if (!default_task_working_directory.has_value() ||
        default_task_working_directory->empty()) {
      return Failure(ProjectServiceErrorType::kInvalidInput,
                     "missing or invalid field: defaultTaskWorkingDirectory");
    }
    project.default_task_working_directory = *default_task_working_directory;
  }
  if (request.contains(kOrdering)) {
    project.ordering = OptionalInt(request, kOrdering, project.ordering);
  }
  if (request.contains(kReadonlyDirectories)) {
    const std::string readonly_directories_error =
        ParseReadonlyDirectories(request, &project.readonly_directories);
    if (!readonly_directories_error.empty()) {
      return Failure(ProjectServiceErrorType::kInvalidInput,
                     readonly_directories_error);
    }
  }

  if (!database_.UpdateProject(id, project)) {
    return Failure(ProjectServiceErrorType::kInternal,
                   "failed to update project");
  }
  const std::optional<Project> updated = database_.GetProject(id);
  if (!updated.has_value()) {
    return Failure(ProjectServiceErrorType::kInternal,
                   "failed to read back the updated project");
  }
  return Success(ProjectToJson(*updated));
}

}  // namespace server
}  // namespace jiaolong
