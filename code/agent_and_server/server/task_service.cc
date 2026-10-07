#include "server/task_service.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace jiaolong {
namespace server {

namespace {

constexpr char kId[] = "id";
constexpr char kProjectId[] = "projectId";
constexpr char kTitle[] = "title";
constexpr char kDescription[] = "description";
constexpr char kWorkingDirectory[] = "workingDirectory";
constexpr char kTokenLimit[] = "tokenLimit";
constexpr char kPriority[] = "priority";
constexpr char kState[] = "state";
constexpr char kAgentWorking[] = "agentWorking";
constexpr char kCurrentRound[] = "currentRound";
constexpr char kTotalTokenUsage[] = "totalTokenUsage";
constexpr char kCreatedAt[] = "createdAt";
constexpr char kUpdatedAt[] = "updatedAt";
constexpr char kReadonlyDirectoryIds[] = "readonlyDirectoryIds";
constexpr char kFailureReason[] = "failureReason";

TaskServiceResult Failure(TaskServiceErrorType type,
                          const std::string& message) {
  TaskServiceResult result;
  result.success = false;
  result.error_type = type;
  result.error_message = message;
  return result;
}

TaskServiceResult Success(nlohmann::json value) {
  TaskServiceResult result;
  result.success = true;
  result.value = std::move(value);
  return result;
}

bool IsValidPriority(const std::string& priority) {
  return priority == "low" || priority == "normal" || priority == "high";
}

bool IsValidState(const std::string& state) {
  return state == "todo" || state == "in_progress" ||
         state == "needs_review" || state == "failed" ||
         state == "completed" || state == "done" || state == "approved";
}

nlohmann::json TaskToJson(const Task& task) {
  return nlohmann::json{
      {kId, task.id},
      {kProjectId, task.project_id},
      {kTitle, task.title},
      {kDescription, task.description},
      {kWorkingDirectory, task.working_directory},
      {kTokenLimit, task.token_limit},
      {kPriority, task.priority},
      {kState, task.state},
      {kCurrentRound, task.current_round},
      {kAgentWorking, task.agent_working},
      {kTotalTokenUsage, task.total_token_usage},
      {kCreatedAt, task.created_at},
      {kUpdatedAt, task.updated_at},
      {kReadonlyDirectoryIds, task.readonly_directories},
      {kFailureReason, task.failure_reason},
  };
}

// Parses the optional `readonlyDirectoryIds` field of a task request into
// `out`. Returns an empty string on success, or a human-readable error message
// when the field is present but malformed (not an array of strings, or with
// duplicate ids).
std::string ParseReadonlyDirectoryIds(const nlohmann::json& request,
                                      std::vector<std::string>* out) {
  if (!request.contains(kReadonlyDirectoryIds)) {
    return "";
  }
  const nlohmann::json& ids_json = request[kReadonlyDirectoryIds];
  if (!ids_json.is_array()) {
    return "field readonlyDirectoryIds must be an array";
  }
  std::vector<std::string> ids;
  for (const nlohmann::json& id_json : ids_json) {
    if (!id_json.is_string()) {
      return "readonlyDirectoryIds must contain strings";
    }
    ids.push_back(id_json.get<std::string>());
  }
  for (size_t i = 0; i < ids.size(); ++i) {
    for (size_t j = i + 1; j < ids.size(); ++j) {
      if (ids[i] == ids[j]) {
        return "readonlyDirectoryIds must not contain duplicate ids";
      }
    }
  }
  *out = std::move(ids);
  return "";
}

// Returns an empty string when every id in `ids` exists in `catalog`, or a
// human-readable error message otherwise.
std::string ValidateReadonlyDirectorySubset(
    const std::vector<std::string>& ids,
    const std::vector<ReadonlyDirectory>& catalog) {
  for (const std::string& id : ids) {
    bool found = false;
    for (const ReadonlyDirectory& directory : catalog) {
      if (directory.id == id) {
        found = true;
        break;
      }
    }
    if (!found) {
      return "readonlyDirectoryIds must be a subset of the project's "
             "readonly directories";
    }
  }
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

// Returns the value of a required integer field, or std::nullopt when the
// field is missing or not an integer.
std::optional<int> RequireInt(const nlohmann::json& json, const char* key) {
  const auto it = json.find(key);
  if (it == json.end() || !it->is_number_integer()) {
    return std::nullopt;
  }
  return it->get<int>();
}

// Returns the value of an optional string field, or `fallback` when the field
// is missing or null.
std::string OptionalString(const nlohmann::json& json, const char* key,
                           const std::string& fallback) {
  const auto it = json.find(key);
  if (it == json.end() || it->is_null()) {
    return fallback;
  }
  if (!it->is_string()) {
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

std::optional<std::string> ValidateTitle(const std::string& title) {
  if (title.empty()) {
    return "title must not be empty";
  }
  return std::nullopt;
}

std::optional<std::string> ValidateWorkingDirectory(
    const std::string& working_directory) {
  if (working_directory.empty()) {
    return "working_directory must not be empty";
  }
  return std::nullopt;
}

}  // namespace

TaskService::TaskService(TaskDatabase& database) : database_(database) {}

TaskService::TaskService(TaskDatabase& database,
                         ProjectDatabase& project_database)
    : database_(database), project_database_(&project_database) {}

TaskServiceResult TaskService::ListTasks(const std::string& project_id,
                                         const std::string& state_filter,
                                         const std::string& sort_by,
                                         const std::string& order,
                                         const std::string& keywords) const {
  const std::vector<Task> tasks =
      database_.ListTasks(project_id, state_filter, sort_by, order, keywords);
  nlohmann::json result = nlohmann::json::array();
  for (const Task& task : tasks) {
    result.push_back(TaskToJson(task));
  }
  return Success(result);
}

TaskServiceResult TaskService::SearchTasks(const std::string& project_id,
                                           const std::string& keywords) const {
  return ListTasks(project_id, "", "", "asc", keywords);
}

TaskServiceResult TaskService::GetTask(const std::string& id) const {
  const std::optional<Task> task = database_.GetTask(id);
  if (!task.has_value()) {
    return Failure(TaskServiceErrorType::kNotFound, "task not found: " + id);
  }
  return Success(TaskToJson(*task));
}

TaskServiceResult TaskService::GetTotalTokenUsage() const {
  return Success(
      nlohmann::json{{kTotalTokenUsage, database_.GetTotalTokenUsage()}});
}

TaskServiceResult TaskService::CreateTask(const nlohmann::json& request) const {
  if (!request.is_object()) {
    return Failure(TaskServiceErrorType::kInvalidInput,
                   "request body must be a JSON object");
  }

  Task task;
  const std::optional<std::string> project_id = RequireString(request, kProjectId);
  if (!project_id.has_value() || project_id->empty()) {
    return Failure(TaskServiceErrorType::kInvalidInput,
                   "missing or invalid field: projectId");
  }
  task.project_id = *project_id;
  const std::optional<std::string> title = RequireString(request, kTitle);
  if (!title.has_value()) {
    return Failure(TaskServiceErrorType::kInvalidInput,
                   "missing or invalid field: title");
  }
  task.title = *title;
  const std::optional<std::string> title_error = ValidateTitle(task.title);
  if (title_error.has_value()) {
    return Failure(TaskServiceErrorType::kInvalidInput, *title_error);
  }
  task.description = OptionalString(request, kDescription, "");
  const std::optional<std::string> working_directory =
      RequireString(request, kWorkingDirectory);
  if (!working_directory.has_value()) {
    return Failure(TaskServiceErrorType::kInvalidInput,
                   "missing or invalid field: workingDirectory");
  }
  task.working_directory = *working_directory;
  const std::optional<std::string> working_directory_error =
      ValidateWorkingDirectory(task.working_directory);
  if (working_directory_error.has_value()) {
    return Failure(TaskServiceErrorType::kInvalidInput,
                   *working_directory_error);
  }
  const std::optional<int> token_limit = RequireInt(request, kTokenLimit);
  if (!token_limit.has_value()) {
    return Failure(TaskServiceErrorType::kInvalidInput,
                   "missing or invalid field: tokenLimit");
  }
  task.token_limit = *token_limit;
  task.priority = OptionalString(request, kPriority, "normal");
  if (!IsValidPriority(task.priority)) {
    return Failure(TaskServiceErrorType::kInvalidInput,
                   "priority must be one of: low, normal, high");
  }

  const std::string readonly_ids_error =
      ParseReadonlyDirectoryIds(request, &task.readonly_directories);
  if (!readonly_ids_error.empty()) {
    return Failure(TaskServiceErrorType::kInvalidInput, readonly_ids_error);
  }
  if (project_database_ != nullptr && !task.readonly_directories.empty()) {
    const std::vector<ReadonlyDirectory> catalog =
        project_database_->ListProjectReadonlyDirectories(task.project_id);
    const std::string subset_error =
        ValidateReadonlyDirectorySubset(task.readonly_directories, catalog);
    if (!subset_error.empty()) {
      return Failure(TaskServiceErrorType::kInvalidInput, subset_error);
    }
  }

  const std::string id = database_.CreateTask(task);
  if (id.empty()) {
    return Failure(TaskServiceErrorType::kInternal, "failed to create task");
  }
  // Keep the associated project's default task working directory in sync with
  // the working directory used for the new task, so the client can pre-fill
  // the next task creation form from the project attribute. This is best
  // effort: a missing project must not fail an otherwise successful creation.
  if (project_database_ != nullptr) {
    project_database_->UpdateProjectDefaultTaskWorkingDirectory(
        task.project_id, task.working_directory);
  }
  const std::optional<Task> created = database_.GetTask(id);
  if (!created.has_value()) {
    return Failure(TaskServiceErrorType::kInternal,
                   "failed to read back the created task");
  }
  return Success(TaskToJson(*created));
}

TaskServiceResult TaskService::UpdateTask(const std::string& id,
                                          const nlohmann::json& request) const {
  if (!request.is_object()) {
    return Failure(TaskServiceErrorType::kInvalidInput,
                   "request body must be a JSON object");
  }

  const std::optional<Task> existing = database_.GetTask(id);
  if (!existing.has_value()) {
    return Failure(TaskServiceErrorType::kNotFound, "task not found: " + id);
  }

  Task task = *existing;
  if (request.contains(kTitle)) {
    const std::optional<std::string> title = RequireString(request, kTitle);
    if (!title.has_value()) {
      return Failure(TaskServiceErrorType::kInvalidInput,
                     "missing or invalid field: title");
    }
    task.title = *title;
    const std::optional<std::string> title_error = ValidateTitle(task.title);
    if (title_error.has_value()) {
      return Failure(TaskServiceErrorType::kInvalidInput, *title_error);
    }
  }
  if (request.contains(kDescription)) {
    const std::optional<std::string> description =
        RequireString(request, kDescription);
    if (!description.has_value()) {
      return Failure(TaskServiceErrorType::kInvalidInput,
                     "missing or invalid field: description");
    }
    task.description = *description;
  }
  if (request.contains(kWorkingDirectory)) {
    const std::optional<std::string> working_directory =
        RequireString(request, kWorkingDirectory);
    if (!working_directory.has_value()) {
      return Failure(TaskServiceErrorType::kInvalidInput,
                     "missing or invalid field: workingDirectory");
    }
    task.working_directory = *working_directory;
    const std::optional<std::string> working_directory_error =
        ValidateWorkingDirectory(task.working_directory);
    if (working_directory_error.has_value()) {
      return Failure(TaskServiceErrorType::kInvalidInput,
                     *working_directory_error);
    }
  }
  if (request.contains(kTokenLimit)) {
    task.token_limit = OptionalInt(request, kTokenLimit, task.token_limit);
  }
  if (request.contains(kPriority)) {
    const std::optional<std::string> priority =
        RequireString(request, kPriority);
    if (!priority.has_value()) {
      return Failure(TaskServiceErrorType::kInvalidInput,
                     "missing or invalid field: priority");
    }
    task.priority = *priority;
    if (!IsValidPriority(task.priority)) {
      return Failure(TaskServiceErrorType::kInvalidInput,
                     "priority must be one of: low, normal, high");
    }
  }
  if (request.contains(kState)) {
    const std::optional<std::string> state = RequireString(request, kState);
    if (!state.has_value()) {
      return Failure(TaskServiceErrorType::kInvalidInput,
                     "missing or invalid field: state");
    }
    task.state = *state;
    if (!IsValidState(task.state)) {
      return Failure(TaskServiceErrorType::kInvalidInput,
                     "state must be one of: todo, in_progress, needs_review, "
                     "failed, completed");
    }
  }
  if (request.contains(kReadonlyDirectoryIds)) {
    const std::string readonly_ids_error =
        ParseReadonlyDirectoryIds(request, &task.readonly_directories);
    if (!readonly_ids_error.empty()) {
      return Failure(TaskServiceErrorType::kInvalidInput, readonly_ids_error);
    }
    if (project_database_ != nullptr && !task.readonly_directories.empty()) {
      const std::vector<ReadonlyDirectory> catalog =
          project_database_->ListProjectReadonlyDirectories(task.project_id);
      const std::string subset_error =
          ValidateReadonlyDirectorySubset(task.readonly_directories, catalog);
      if (!subset_error.empty()) {
        return Failure(TaskServiceErrorType::kInvalidInput, subset_error);
      }
    }
  }

  if (!database_.UpdateTask(id, task)) {
    return Failure(TaskServiceErrorType::kNotFound, "task not found: " + id);
  }
  // Keep the associated project's default task working directory in sync with
  // the working directory used for the edited task, so the client can pre-fill
  // the next task creation form from the project attribute. This is best
  // effort: a missing project must not fail an otherwise successful update.
  if (project_database_ != nullptr) {
    project_database_->UpdateProjectDefaultTaskWorkingDirectory(
        task.project_id, task.working_directory);
  }
  const std::optional<Task> updated = database_.GetTask(id);
  if (!updated.has_value()) {
    return Failure(TaskServiceErrorType::kInternal,
                   "failed to read back the updated task");
  }
  return Success(TaskToJson(*updated));
}

TaskServiceResult TaskService::StartWork(const std::string& id) const {
  if (!database_.StartWork(id)) {
    return Failure(TaskServiceErrorType::kNotFound, "task not found: " + id);
  }
  const std::optional<Task> updated = database_.GetTask(id);
  if (!updated.has_value()) {
    return Failure(TaskServiceErrorType::kInternal,
                   "failed to read back the updated task");
  }
  return Success(TaskToJson(*updated));
}

TaskServiceResult TaskService::CompleteWork(const std::string& id) const {
  if (!database_.CompleteWork(id)) {
    return Failure(TaskServiceErrorType::kNotFound, "task not found: " + id);
  }
  const std::optional<Task> updated = database_.GetTask(id);
  if (!updated.has_value()) {
    return Failure(TaskServiceErrorType::kInternal,
                   "failed to read back the updated task");
  }
  return Success(TaskToJson(*updated));
}

TaskServiceResult TaskService::FailWork(const std::string& id) const {
  if (!database_.FailWork(id)) {
    return Failure(TaskServiceErrorType::kNotFound, "task not found: " + id);
  }
  const std::optional<Task> updated = database_.GetTask(id);
  if (!updated.has_value()) {
    return Failure(TaskServiceErrorType::kInternal,
                   "failed to read back the updated task");
  }
  return Success(TaskToJson(*updated));
}

TaskServiceResult TaskService::ApproveTask(const std::string& id) const {
  if (!database_.ApproveTask(id)) {
    return Failure(TaskServiceErrorType::kNotFound, "task not found: " + id);
  }
  const std::optional<Task> updated = database_.GetTask(id);
  if (!updated.has_value()) {
    return Failure(TaskServiceErrorType::kInternal,
                   "failed to read back the updated task");
  }
  return Success(TaskToJson(*updated));
}

TaskServiceResult TaskService::ConfirmCompletion(const std::string& id) const {
  const std::optional<Task> existing = database_.GetTask(id);
  if (!existing.has_value()) {
    return Failure(TaskServiceErrorType::kNotFound, "task not found: " + id);
  }
  if (existing->state != "needs_review") {
    return Failure(TaskServiceErrorType::kIllegalState,
                   "task is not in needs_review state: " + id);
  }
  if (!database_.ConfirmCompletion(id)) {
    return Failure(TaskServiceErrorType::kNotFound, "task not found: " + id);
  }
  const std::optional<Task> updated = database_.GetTask(id);
  if (!updated.has_value()) {
    return Failure(TaskServiceErrorType::kInternal,
                   "failed to read back the updated task");
  }
  return Success(TaskToJson(*updated));
}

TaskServiceResult TaskService::DeleteTask(const std::string& id) const {
  if (!database_.DeleteTask(id)) {
    return Failure(TaskServiceErrorType::kNotFound, "task not found: " + id);
  }
  return Success(nlohmann::json());
}

}  // namespace server
}  // namespace jiaolong