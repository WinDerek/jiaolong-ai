#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "server/project_database.h"
#include "server/task_database.h"

namespace jiaolong {
namespace server {

// Kinds of failures a TaskService operation can report. Each maps to an HTTP
// status code in the REST layer.
enum class TaskServiceErrorType {
  kNone,
  // The referenced task does not exist (HTTP 404).
  kNotFound,
  // The request body is missing required fields or contains invalid values
  // (HTTP 400).
  kInvalidInput,
  // An operation is attempted on a task whose current state does not allow
  // the transition (HTTP 409).
  kIllegalState,
  // Unexpected internal failure (HTTP 500).
  kInternal,
};

// Result of a TaskService operation. When `success` is false, `error_type`
// and `error_message` describe the failure.
struct TaskServiceResult {
  bool success = false;
  nlohmann::json value;
  TaskServiceErrorType error_type = TaskServiceErrorType::kInternal;
  std::string error_message;
};

// Business logic for the task-management REST APIs. TaskService translates
// request payloads into TaskDatabase operations and serializes the results
// back to JSON. It does not own any storage; persistence is delegated to the
// TaskDatabase it is constructed with, and (when provided) to the
// ProjectDatabase used to keep project metadata in sync with task data.
class TaskService {
 public:
  explicit TaskService(TaskDatabase& database);
  TaskService(TaskDatabase& database, ProjectDatabase& project_database);

  // GET /api/tasks?projectId=&state=&sort=id|priority|status&order=asc|desc
  // Lists the tasks of the given project as a JSON array. `projectId` is
  // mandatory: only tasks that belong to the project are returned. Tasks are
  // optionally filtered by state, by keywords (a whitespace-separated list
  // where every keyword must appear in the title or description) and sorted by
  // id/priority/status in asc/desc order. When no sort is specified the tasks
  // are ordered by status: needs_review, in_progress, todo, failed, then
  // completed (done/approved) last.
  TaskServiceResult ListTasks(const std::string& project_id,
                              const std::string& state_filter,
                              const std::string& sort_by,
                              const std::string& order,
                              const std::string& keywords = "") const;

  // GET /api/tasks/search?projectId=&keywords=
  // Searches the tasks of the given project by keywords and returns the
  // matching tasks as a JSON array. `projectId` is mandatory. `keywords` is
  // split on whitespace and a task matches when its title or description
  // contains every keyword (case-insensitive). An empty `keywords` string
  // matches every task of the project.
  TaskServiceResult SearchTasks(const std::string& project_id,
                                const std::string& keywords) const;

  // GET /api/tasks/{id}. Returns a single task as JSON, or reports
  // TaskServiceErrorType::kNotFound when no task with the given id exists.
  TaskServiceResult GetTask(const std::string& id) const;

  // GET /api/tasks/total-token-usage. Returns the total token usage of all
  // tasks as JSON: {"totalTokenUsage": <int>}.
  TaskServiceResult GetTotalTokenUsage() const;

  // POST /api/tasks. Creates a task under the project given by the mandatory
  // `projectId` field of the JSON request body and returns the created task
  // as JSON. When a ProjectDatabase is configured, the associated project's
  // default task working directory is also updated to the task's working
  // directory so the client can pre-fill the next task creation form from the
  // project attribute.
  TaskServiceResult CreateTask(const nlohmann::json& request) const;

  // PUT /api/tasks/{id}. Updates the editable fields (title, description,
  // working_directory, token_limit, priority, state) of an existing task.
  // Only the fields present in the request body are changed. When a
  // ProjectDatabase is configured, the associated project's default task
  // working directory is also updated to the task's working directory so the
  // client can pre-fill the next task creation form from the project
  // attribute. Returns the updated task as JSON.
  TaskServiceResult UpdateTask(const std::string& id,
                               const nlohmann::json& request) const;

  // POST /api/tasks/{id}/work. Marks the task as in_progress, sets
  // agent_working to true and increments its current_round. Returns the
  // updated task as JSON.
  TaskServiceResult StartWork(const std::string& id) const;

  // Called when the agent subprocess finishes working on a task: clears
  // agent_working and moves the task to needs_review (the LLM stopped
  // naturally). Returns the updated task as JSON.
  TaskServiceResult CompleteWork(const std::string& id) const;

  // Called when the agent subprocess finishes working on a task but the LLM
  // did not stop naturally: clears agent_working and moves the task to failed.
  // Returns the updated task as JSON.
  TaskServiceResult FailWork(const std::string& id) const;

  // POST /api/tasks/{id}/approve. Marks the task as done. Returns the updated
  // task as JSON.
  TaskServiceResult ApproveTask(const std::string& id) const;

  // POST /api/tasks/{id}/confirm-completion. Marks a task as completed. Only
  // legal when the task is currently in the needs_review state; reports
  // TaskServiceErrorType::kIllegalState otherwise. Returns the updated task as
  // JSON.
  TaskServiceResult ConfirmCompletion(const std::string& id) const;

  // DELETE /api/tasks/{id}. Deletes the task. Reports
  // TaskServiceErrorType::kNotFound when the task does not exist.
  TaskServiceResult DeleteTask(const std::string& id) const;

 private:
  TaskDatabase& database_;
  // Optional project storage used to keep the associated project's default
  // task working directory in sync when a task is created or edited. May be
  // null when the service is used without project persistence (e.g. in unit
  // tests).
  ProjectDatabase* project_database_ = nullptr;
};

}  // namespace server
}  // namespace jiaolong