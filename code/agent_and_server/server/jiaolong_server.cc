#include "server/jiaolong_server.h"

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "server/client_credential_manager.h"
#include "server/project_service.h"
#include "server/settings_service.h"
#include "server/task_service.h"

namespace jiaolong {
namespace server {

namespace {

// Returns the current local time formatted as "YYYY-MM-DD HH:MM:SS".
std::string CurrentTimeString() {
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

// Builds the placeholder JSON body returned by endpoints whose services are
// not implemented yet.
std::string PlaceholderBody(const std::string& message) {
  return "{\"message\":\"" + message + "\"}";
}

// Builds an error response body: {"error": "<message>"}.
std::string ErrorBody(const std::string& message) {
  return nlohmann::json{{"error", message}}.dump();
}

// Returns true for the auth endpoints that must be reachable without a bearer
// token (the OAuth 2.0 login and refresh endpoints).
bool IsPublicAuthPath(const std::string& method, const std::string& path) {
  return (method == "POST" && path == "/api/auth/login") ||
         (method == "POST" && path == "/api/auth/refresh");
}

// Extracts the bearer token from the Authorization header, or returns an empty
// string when the header is missing or is not a Bearer token.
std::string BearerToken(const httplib::Request& req) {
  constexpr char kBearerPrefix[] = "Bearer ";
  const std::string auth_header = req.get_header_value("Authorization");
  if (auth_header.compare(0, sizeof(kBearerPrefix) - 1, kBearerPrefix) != 0) {
    return "";
  }
  return auth_header.substr(sizeof(kBearerPrefix) - 1);
}

// Serializes a token pair as the OAuth 2.0 login/refresh response body:
//   {
//     "accessToken": "...",
//     "refreshToken": "...",
//     "tokenType": "Bearer",
//     "expiresIn": <seconds>,
//     "refreshExpiresIn": <seconds>
//   }
nlohmann::json TokenPairToJson(const ClientTokenPair& pair) {
  return nlohmann::json{
      {"accessToken", pair.access_token},
      {"refreshToken", pair.refresh_token},
      {"tokenType", "Bearer"},
      {"expiresIn", pair.access_token_lifetime.count()},
      {"refreshExpiresIn", pair.refresh_token_lifetime.count()},
  };
}

// Writes the result of a ToolVcs operation (status + captured stdout) as an
// HTTP JSON response. Success responses carry {"stdout": "..."}; failures use
// the standard {"error": "..."} body with an appropriate status code.
void WriteToolResult(const std::shared_ptr<ToolUseStatus>& status,
    const std::string& stdout_output, httplib::Response& res) {
  switch (status->type_) {
    case ToolUseStatusType::kSuccess:
      res.set_content(nlohmann::json{{"stdout", stdout_output}}.dump(),
                      "application/json");
      break;
    case ToolUseStatusType::kInvalidInput:
      res.status = 400;
      res.set_content(ErrorBody(
          std::static_pointer_cast<ToolUseInvalidInput>(status)->error_message_),
          "application/json");
      break;
    case ToolUseStatusType::kExecutionFailure:
      res.status = 500;
      res.set_content(ErrorBody(
          std::static_pointer_cast<ToolUseExecutionFailure>(status)->error_message_),
          "application/json");
      break;
  }
}

// Parses a JSON request body without relying on exceptions. Callers must check
// `is_discarded()` on the returned value to detect a parse failure.
nlohmann::json ParseJsonBody(const std::string& body) {
  return nlohmann::json::parse(body, nullptr, false);
}

// Returns the value of a path parameter, or an empty string when the
// parameter is not present.
std::string PathParam(const httplib::Request& req, const std::string& name) {
  const auto it = req.path_params.find(name);
  if (it == req.path_params.end()) {
    return "";
  }
  return it->second;
}

// Maps a TaskServiceErrorType to the HTTP status code used in responses.
int TaskServiceErrorTypeToStatus(TaskServiceErrorType type) {
  switch (type) {
    case TaskServiceErrorType::kNotFound:
      return 404;
    case TaskServiceErrorType::kInvalidInput:
      return 400;
    case TaskServiceErrorType::kIllegalState:
      return 409;
    case TaskServiceErrorType::kNone:
    case TaskServiceErrorType::kInternal:
      return 500;
  }
  return 500;
}

// Writes a TaskServiceResult as an HTTP response. On success the response
// carries `result.value` (when it is not null) with the given success status
// code when it is non-zero; on failure the mapped error status and body are
// written.
void WriteTaskServiceResult(const TaskServiceResult& result,
    int success_status, httplib::Response& res) {
  if (result.success) {
    if (success_status != 0) {
      res.status = success_status;
    }
    if (!result.value.is_null()) {
      res.set_content(result.value.dump(), "application/json");
    }
    return;
  }
  res.status = TaskServiceErrorTypeToStatus(result.error_type);
  res.set_content(ErrorBody(result.error_message), "application/json");
}

// Maps a ProjectServiceErrorType to the HTTP status code used in responses.
int ProjectServiceErrorTypeToStatus(ProjectServiceErrorType type) {
  switch (type) {
    case ProjectServiceErrorType::kNotFound:
      return 404;
    case ProjectServiceErrorType::kInvalidInput:
      return 400;
    case ProjectServiceErrorType::kNone:
    case ProjectServiceErrorType::kInternal:
      return 500;
  }
  return 500;
}

// Writes a ProjectServiceResult as an HTTP response. On success the response
// carries `result.value` (when it is not null) with the given success status
// code when it is non-zero; on failure the mapped error status and body are
// written.
void WriteProjectServiceResult(const ProjectServiceResult& result,
    int success_status, httplib::Response& res) {
  if (result.success) {
    if (success_status != 0) {
      res.status = success_status;
    }
    if (!result.value.is_null()) {
      res.set_content(result.value.dump(), "application/json");
    }
    return;
  }
  res.status = ProjectServiceErrorTypeToStatus(result.error_type);
  res.set_content(ErrorBody(result.error_message), "application/json");
}

// Searches the `~/.jiaolong/sessions` folder for the session history JSON
// file associated with the given task id and returns its path. Session history
// files are matched by checking the `taskId` field of the JSON file; when
// several files match, the most recently modified one is selected. Returns
// std::nullopt when HOME is not set, the sessions folder does not exist, or
// no matching session history file is found.
std::optional<std::filesystem::path> FindSessionHistoryFilePath(
    const std::string& task_id) {
  const char* home_directory = std::getenv("HOME");
  if (home_directory == nullptr) {
    return std::nullopt;
  }

  const std::filesystem::path session_directory =
      std::filesystem::path(home_directory) / ".jiaolong" / "sessions";
  if (!std::filesystem::is_directory(session_directory)) {
    return std::nullopt;
  }

  std::filesystem::path selected_path;
  std::filesystem::file_time_type selected_time{};
  bool has_selected = false;
  for (const auto& entry :
      std::filesystem::directory_iterator(session_directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    std::ifstream session_file(entry.path());
    if (!session_file.is_open()) {
      continue;
    }
    std::stringstream buffer;
    buffer << session_file.rdbuf();
    const nlohmann::json session_history_json =
        nlohmann::json::parse(buffer.str(), nullptr, false);
    if (session_history_json.is_discarded() ||
        !session_history_json.contains("taskId") ||
        session_history_json["taskId"] != task_id) {
      continue;
    }
    if (!has_selected || entry.last_write_time() > selected_time) {
      selected_path = entry.path();
      selected_time = entry.last_write_time();
      has_selected = true;
    }
  }
  if (!has_selected) {
    return std::nullopt;
  }
  return selected_path;
}

}  // namespace

JiaolongServer::JiaolongServer(const std::string& jiaolong_cli_path,
    const std::string& jj_executable_path,
    const std::string& github_token,
    const std::string& gh_executable_path,
    const std::string& socks_proxy,
    const std::string& mac_system_health_data_server_domain,
    const std::string& username,
    const std::string& password) :
    jiaolong_cli_path_(jiaolong_cli_path),
    tool_vcs_init_branch_(jj_executable_path, socks_proxy),
    tool_vcs_upload_commit_(jj_executable_path, github_token,
        gh_executable_path, socks_proxy),
    client_credential_manager_(username, password),
    system_health_data_manager_(mac_system_health_data_server_domain) {
  // Start the system health perception loop as soon as the server boots up so
  // system health data keeps being gathered even before the HTTP server starts
  // listening.
  system_health_data_manager_.Start();
}

JiaolongServer::~JiaolongServer() {
  system_health_data_manager_.Stop();
}

void JiaolongServer::RegisterRoutes() {
  RegisterLogging();
  RegisterAuthRoutes();
  RegisterTaskManagementRoutes();
  RegisterProjectRoutes();
  RegisterSettingsRoutes();
  RegisterServerRoutes();
}

void JiaolongServer::RegisterAuthRoutes() {
  // Protect every REST API: each request must carry a valid bearer access
  // token issued by ClientCredentialManager, except the OAuth 2.0 login and
  // refresh endpoints themselves.
  http_server_.set_pre_routing_handler([this](const httplib::Request& req,
                                              httplib::Response& res) {
    if (IsPublicAuthPath(req.method, req.path)) {
      return httplib::Server::HandlerResponse::Unhandled;
    }
    const std::string token = BearerToken(req);
    if (!token.empty() &&
        client_credential_manager_.ValidateAccessToken(token)) {
      return httplib::Server::HandlerResponse::Unhandled;
    }
    res.status = 401;
    res.set_content(ErrorBody("Unauthorized: a valid bearer token is required"),
                    "application/json");
    return httplib::Server::HandlerResponse::Handled;
  });

  // POST /api/auth/login
  // OAuth 2.0 resource-owner-password login. Body:
  //   {"username": "...", "password": "..."}
  // Returns the access token and refresh token pair.
  http_server_.Post("/api/auth/login", [this](const httplib::Request& req,
                                              httplib::Response& res) {
    const nlohmann::json request = ParseJsonBody(req.body);
    if (request.is_discarded()) {
      res.status = 400;
      res.set_content(ErrorBody("request body is not valid JSON"),
                      "application/json");
      return;
    }
    const std::string username = request.value("username", "");
    const std::string password = request.value("password", "");
    const std::optional<ClientTokenPair> pair =
        client_credential_manager_.Login(username, password);
    if (!pair.has_value()) {
      res.status = 401;
      res.set_content(ErrorBody("Invalid username or password"),
                      "application/json");
      return;
    }
    res.set_content(TokenPairToJson(*pair).dump(), "application/json");
  });

  // POST /api/auth/refresh
  // OAuth 2.0 refresh-token grant. Body:
  //   {"refreshToken": "..."}
  // Returns a fresh token pair (rotated; the old pair is invalidated).
  http_server_.Post("/api/auth/refresh", [this](const httplib::Request& req,
                                                httplib::Response& res) {
    const nlohmann::json request = ParseJsonBody(req.body);
    if (request.is_discarded()) {
      res.status = 400;
      res.set_content(ErrorBody("request body is not valid JSON"),
                      "application/json");
      return;
    }
    const std::string refresh_token = request.value("refreshToken", "");
    const std::optional<ClientTokenPair> pair =
        client_credential_manager_.Refresh(refresh_token);
    if (!pair.has_value()) {
      res.status = 401;
      res.set_content(ErrorBody("Invalid or expired refresh token"),
                      "application/json");
      return;
    }
    res.set_content(TokenPairToJson(*pair).dump(), "application/json");
  });

  // POST /api/auth/logout
  // Revokes the current token pair so the client must log in again.
  http_server_.Post("/api/auth/logout", [this](const httplib::Request& req,
                                               httplib::Response& res) {
    const std::string token = BearerToken(req);
    if (!token.empty()) {
      client_credential_manager_.Logout(token);
    }
    res.set_content(nlohmann::json{{"message", "Logged out"}}.dump(),
                    "application/json");
  });
}

void JiaolongServer::RegisterLogging() {
  // Log every HTTP request (method, target and client address) together with
  // the resulting status code to stdout.
  http_server_.set_logger([](const httplib::Request& req,
                     const httplib::Response& res) {
    std::cout << "[" << CurrentTimeString() << "] " << req.method << " "
              << req.target << " from " << req.remote_addr << " -> "
              << res.status << std::endl;
  });
}

void JiaolongServer::RegisterTaskManagementRoutes() {
  // GET /api/tasks?projectId=&state=&sort=id|priority|status&order=asc|desc
  // List/filter/sort the tasks that belong to a project. `projectId` is
  // mandatory. Without an explicit sort the tasks are ordered by status:
  // needs_review, in_progress, todo, failed, completed last.
  http_server_.Get("/api/tasks", [this](const httplib::Request& req,
                                        httplib::Response& res) {
    const std::string project_id = req.get_param_value("projectId");
    if (project_id.empty()) {
      res.status = 400;
      res.set_content(ErrorBody("missing projectId"), "application/json");
      return;
    }
    const std::string state = req.get_param_value("state");
    const std::string sort = req.get_param_value("sort");
    const std::string order = req.get_param_value("order");
    const TaskServiceResult result =
        task_service_.ListTasks(project_id, state, sort, order);
    if (!result.success) {
      WriteTaskServiceResult(result, 0, res);
      return;
    }
    res.set_content(result.value.dump(), "application/json");
  });

  // GET /api/tasks/total-token-usage
  // Returns the total token usage of all tasks:
  //   {"totalTokenUsage": <int>}
  http_server_.Get("/api/tasks/total-token-usage",
      [this](const httplib::Request&, httplib::Response& res) {
        const TaskServiceResult result = task_service_.GetTotalTokenUsage();
        WriteTaskServiceResult(result, 0, res);
      });

  // GET /api/tasks/search?projectId=&keywords=
  // Searches the tasks of a project by keywords and returns the matching tasks
  // as a JSON array. `projectId` is mandatory. `keywords` is split on
  // whitespace and a task matches when its title or description contains every
  // keyword (case-insensitive). Backs the search box on the dashboard. This
  // route must be registered before `/api/tasks/:id` so it is not captured by
  // the task-detail route.
  http_server_.Get("/api/tasks/search",
      [this](const httplib::Request& req, httplib::Response& res) {
        const std::string project_id = req.get_param_value("projectId");
        if (project_id.empty()) {
          res.status = 400;
          res.set_content(ErrorBody("missing projectId"), "application/json");
          return;
        }
        const std::string keywords = req.get_param_value("keywords");
        const TaskServiceResult result =
            task_service_.SearchTasks(project_id, keywords);
        WriteTaskServiceResult(result, 0, res);
      });

  // GET /api/tasks/{id}
  // Returns the latest detail of a single task. Backs the task detail page,
  // which reloads the task from the server every time it is entered instead of
  // relying on the task information collected on the dashboard page.
  http_server_.Get("/api/tasks/:id",
      [this](const httplib::Request& req, httplib::Response& res) {
        const std::string id = PathParam(req, "id");
        if (id.empty()) {
          res.status = 400;
          res.set_content(ErrorBody("missing task id"), "application/json");
          return;
        }
        const TaskServiceResult result = task_service_.GetTask(id);
        WriteTaskServiceResult(result, 0, res);
      });

  // POST /api/tasks
  // Create a task under a project. The mandatory `projectId` body field
  // identifies the project the task belongs to.
  http_server_.Post("/api/tasks", [this](const httplib::Request& req,
                                         httplib::Response& res) {
    const nlohmann::json request = ParseJsonBody(req.body);
    if (request.is_discarded()) {
      res.status = 400;
      res.set_content(ErrorBody("request body is not valid JSON"),
                      "application/json");
      return;
    }
    const TaskServiceResult result = task_service_.CreateTask(request);
    WriteTaskServiceResult(result, 201, res);
  });

  // PUT /api/tasks/{id}
  // Modify a task.
  http_server_.Put("/api/tasks/:id", [this](const httplib::Request& req,
                                            httplib::Response& res) {
    const nlohmann::json request = ParseJsonBody(req.body);
    if (request.is_discarded()) {
      res.status = 400;
      res.set_content(ErrorBody("request body is not valid JSON"),
                      "application/json");
      return;
    }
    const std::string id = PathParam(req, "id");
    if (id.empty()) {
      res.status = 400;
      res.set_content(ErrorBody("missing task id"), "application/json");
      return;
    }
    const TaskServiceResult result = task_service_.UpdateTask(id, request);
    WriteTaskServiceResult(result, 0, res);
  });

  // DELETE /api/tasks/{id}
  // Delete a task.
  http_server_.Delete("/api/tasks/:id",
            [this](const httplib::Request& req, httplib::Response& res) {
              const std::string id = PathParam(req, "id");
              if (id.empty()) {
                res.status = 400;
                res.set_content(ErrorBody("missing task id"),
                                "application/json");
                return;
              }
              const TaskServiceResult result = task_service_.DeleteTask(id);
              WriteTaskServiceResult(result, 204, res);
            });

  // POST /api/tasks/{id}/work
  // Let the agent work on the task (round N+1). The task is marked as being
  // worked on by the agent and a background sub-process is started to run
  // `jiaolong_cli --non_interactive --task_id {id}`. The optional `resume`
  // argument (query parameter or JSON body boolean field, default false) tells
  // the agent to resume previous work on the task from the persisted session
  // history; when it is not set a fresh round is started. When the sub-process
  // finishes, the task is updated (agent_working cleared, state ->
  // needs_review when the LLM stopped naturally, failed otherwise).
  http_server_.Post("/api/tasks/:id/work",
            [this](const httplib::Request& req, httplib::Response& res) {
              const std::string id = PathParam(req, "id");
              if (id.empty()) {
                res.status = 400;
                res.set_content(ErrorBody("missing task id"),
                                "application/json");
                return;
              }
              bool resume = false;
              const std::string resume_param = req.get_param_value("resume");
              if (resume_param == "true" || resume_param == "1") {
                resume = true;
              }
              const nlohmann::json request = ParseJsonBody(req.body);
              if (!request.is_discarded() && request.is_object() &&
                  request.contains("resume") && request["resume"].is_boolean()) {
                resume = resume || request["resume"].get<bool>();
              }
              const TaskServiceResult result = task_service_.StartWork(id);
              if (!result.success) {
                WriteTaskServiceResult(result, 0, res);
                return;
              }
              if (!LaunchAgentSubprocess(id, resume)) {
                res.status = 500;
                res.set_content(
                    ErrorBody("failed to launch agent subprocess"),
                    "application/json");
                return;
              }
              res.set_content(result.value.dump(), "application/json");
            });

  // POST /api/tasks/{id}/init-branch
  // Initialize a new branch for the task by running `jj git fetch`,
  // `jj new main@origin` and `jj bookmark create task_<task_id>` inside the
  // task's working directory. Backs the "Init Branch" button on the task
  // detail page.
  http_server_.Post("/api/tasks/:id/init-branch",
            [this](const httplib::Request& req, httplib::Response& res) {
              const std::string id = PathParam(req, "id");
              if (id.empty()) {
                res.status = 400;
                res.set_content(ErrorBody("missing task id"),
                                "application/json");
                return;
              }
              const std::optional<Task> task = task_database_.GetTask(id);
              if (!task.has_value()) {
                res.status = 404;
                res.set_content(ErrorBody("task not found: " + id),
                                "application/json");
                return;
              }
              const auto status_and_output = tool_vcs_init_branch_.InitBranch(
                  ToolVcsInitBranchInput(task->id, task->working_directory));
              WriteToolResult(status_and_output.first,
                  status_and_output.second.stdout_output_, res);
            });

  // GET /api/tasks/{id}/session-history
  // Returns the session history JSON data of a task. The server searches the
  // `~/.jiaolong/sessions` folder for the session history JSON file whose
  // `taskId` field matches the task id (when several match, the most recently
  // modified file is returned) and returns its content as-is.
  http_server_.Get("/api/tasks/:id/session-history",
      [](const httplib::Request& req, httplib::Response& res) {
        const std::string id = PathParam(req, "id");
        if (id.empty()) {
          res.status = 400;
          res.set_content(ErrorBody("missing task id"), "application/json");
          return;
        }
        const std::optional<std::filesystem::path> session_file_path =
            FindSessionHistoryFilePath(id);
        if (!session_file_path.has_value()) {
          res.status = 404;
          res.set_content(
              ErrorBody("session history not found for task: " + id),
              "application/json");
          return;
        }
        std::ifstream session_file(*session_file_path);
        if (!session_file.is_open()) {
          res.status = 500;
          res.set_content(
              ErrorBody("failed to read session history file: " +
                        session_file_path->string()),
              "application/json");
          return;
        }
        std::stringstream buffer;
        buffer << session_file.rdbuf();
        const nlohmann::json session_history_json =
            nlohmann::json::parse(buffer.str(), nullptr, false);
        if (session_history_json.is_discarded()) {
          res.status = 500;
          res.set_content(
              ErrorBody("session history file is not valid JSON: " +
                        session_file_path->string()),
              "application/json");
          return;
        }
        res.set_content(session_history_json.dump(2), "application/json");
      });

  // GET /api/tasks/{id}/commits
  // List round commits of a task.
  http_server_.Get("/api/tasks/:id/commits",
           [](const httplib::Request&, httplib::Response& res) {
             res.set_content(PlaceholderBody("list task commits: not implemented"),
                             "application/json");
           });

  // GET /api/commits/{hash}/diff
  // Get changed files and diff content.
  http_server_.Get("/api/commits/:hash/diff",
           [](const httplib::Request&, httplib::Response& res) {
             res.set_content(PlaceholderBody("get commit diff: not implemented"),
                             "application/json");
           });

  // POST /api/commits/{hash}/comments
  // Post a commit comment.
  http_server_.Post("/api/commits/:hash/comments",
            [](const httplib::Request&, httplib::Response& res) {
              res.set_content(PlaceholderBody("post commit comment: not implemented"),
                              "application/json");
            });

  // POST /api/commits/{hash}/line-comments
  // Post a line comment (file_path, line_number).
  http_server_.Post("/api/commits/:hash/line-comments",
            [](const httplib::Request&, httplib::Response& res) {
              res.set_content(PlaceholderBody("post line comment: not implemented"),
                              "application/json");
            });

  // PATCH /api/comments/{id}/state
  // Resolve/unresolve a comment (user only).
  http_server_.Patch("/api/comments/:id/state",
             [](const httplib::Request&, httplib::Response& res) {
               res.set_content(PlaceholderBody("update comment state: not implemented"),
                               "application/json");
             });

  // GET /api/tasks/{id}/comments
  // List all comments of a task.
  http_server_.Get("/api/tasks/:id/comments",
           [](const httplib::Request&, httplib::Response& res) {
             res.set_content(PlaceholderBody("list task comments: not implemented"),
                             "application/json");
           });

  // POST /api/tasks/{id}/approve
  // Approve; squash all commits into one.
  http_server_.Post("/api/tasks/:id/approve",
            [this](const httplib::Request& req, httplib::Response& res) {
              const std::string id = PathParam(req, "id");
              if (id.empty()) {
                res.status = 400;
                res.set_content(ErrorBody("missing task id"),
                                "application/json");
                return;
              }
              const TaskServiceResult result = task_service_.ApproveTask(id);
              WriteTaskServiceResult(result, 0, res);
            });

  // POST /api/tasks/{id}/confirm-completion
  // Confirm task completion. Only legal when the task is currently in the
  // needs_review state; moves the task to completed.
  http_server_.Post("/api/tasks/:id/confirm-completion",
            [this](const httplib::Request& req, httplib::Response& res) {
              const std::string id = PathParam(req, "id");
              if (id.empty()) {
                res.status = 400;
                res.set_content(ErrorBody("missing task id"),
                                "application/json");
                return;
              }
              const TaskServiceResult result =
                  task_service_.ConfirmCompletion(id);
              WriteTaskServiceResult(result, 0, res);
            });
}

void JiaolongServer::RegisterProjectRoutes() {
  // GET /api/projects
  // List all projects ordered by `ordering` ascending, so the first project
  // (the one with the least ordering value) is the client's default/current
  // project. Backs the dashboard project switcher.
  http_server_.Get("/api/projects",
      [this](const httplib::Request&, httplib::Response& res) {
        const ProjectServiceResult result = project_service_.ListProjects();
        WriteProjectServiceResult(result, 0, res);
      });

  // POST /api/projects
  // Create a project. Body requires `name` and `defaultTaskWorkingDirectory`;
  // `description` (default "") and `ordering` (default 0) are optional.
  http_server_.Post("/api/projects",
      [this](const httplib::Request& req, httplib::Response& res) {
        const nlohmann::json request = ParseJsonBody(req.body);
        if (request.is_discarded()) {
          res.status = 400;
          res.set_content(ErrorBody("request body is not valid JSON"),
                          "application/json");
          return;
        }
        const ProjectServiceResult result =
            project_service_.CreateProject(request);
        WriteProjectServiceResult(result, 201, res);
      });

  // PUT /api/projects/{id}
  // Update a project. Body fields are optional; when present,
  // `readonlyDirectories` replaces the project's readonly-directory catalog.
  http_server_.Put("/api/projects/:id",
      [this](const httplib::Request& req, httplib::Response& res) {
        const nlohmann::json request = ParseJsonBody(req.body);
        if (request.is_discarded()) {
          res.status = 400;
          res.set_content(ErrorBody("request body is not valid JSON"),
                          "application/json");
          return;
        }
        const std::string id = PathParam(req, "id");
        if (id.empty()) {
          res.status = 400;
          res.set_content(ErrorBody("missing project id"), "application/json");
          return;
        }
        const ProjectServiceResult result =
            project_service_.UpdateProject(id, request);
        WriteProjectServiceResult(result, 0, res);
      });

  // GET /api/projects/{id}
  // Returns the latest detail of a single project (including its
  // readonly-directory catalog). Backs the project detail page, which reloads
  // the project from the server every time it is entered.
  http_server_.Get("/api/projects/:id",
      [this](const httplib::Request& req, httplib::Response& res) {
        const std::string id = PathParam(req, "id");
        if (id.empty()) {
          res.status = 400;
          res.set_content(ErrorBody("missing project id"), "application/json");
          return;
        }
        const ProjectServiceResult result = project_service_.GetProject(id);
        WriteProjectServiceResult(result, 0, res);
      });

  // POST /api/projects/{id}/readonly-directories
  // Appends a new readonly directory to a project's catalog. Body requires
  // `alias` and `realPath`; `description` (default "") is optional. Backs the
  // add button on the project detail page.
  http_server_.Post("/api/projects/:id/readonly-directories",
      [this](const httplib::Request& req, httplib::Response& res) {
        const nlohmann::json request = ParseJsonBody(req.body);
        if (request.is_discarded()) {
          res.status = 400;
          res.set_content(ErrorBody("request body is not valid JSON"),
                          "application/json");
          return;
        }
        const std::string id = PathParam(req, "id");
        if (id.empty()) {
          res.status = 400;
          res.set_content(ErrorBody("missing project id"), "application/json");
          return;
        }
        const ProjectServiceResult result =
            project_service_.AddReadonlyDirectory(id, request);
        WriteProjectServiceResult(result, 201, res);
      });
}

void JiaolongServer::RegisterSettingsRoutes() {
  // GET /api/settings/llm-providers
  // List LLM providers and the index of the enabled one.
  http_server_.Get("/api/settings/llm-providers",
      [this](const httplib::Request&, httplib::Response& res) {
        const SettingsResult result = settings_service_.ListLlmProviders();
        if (!result.success) {
          res.status = result.invalid_request ? 400 : 500;
          res.set_content(ErrorBody(result.error_message), "application/json");
          return;
        }
        res.set_content(result.value.dump(), "application/json");
      });

  // PUT /api/settings/llm-providers
  // Set the enabled LLM provider by its index in the llmProviders array.
  // Request body: {"enabledLlmProviderIndex": <int>}.
  http_server_.Put("/api/settings/llm-providers",
      [this](const httplib::Request& req, httplib::Response& res) {
        const nlohmann::json request = ParseJsonBody(req.body);
        if (request.is_discarded()) {
          res.status = 400;
          res.set_content(ErrorBody("request body is not valid JSON"),
                          "application/json");
          return;
        }
        const SettingsResult result =
            settings_service_.SetEnabledLlmProvider(request);
        if (!result.success) {
          res.status = result.invalid_request ? 400 : 500;
          res.set_content(ErrorBody(result.error_message), "application/json");
          return;
        }
        res.set_content(result.value.dump(), "application/json");
      });
}

void JiaolongServer::RegisterServerRoutes() {
  // GET /api/server/system-health
  // Returns the latest server system health data (currently the CPU
  // temperature) as the last 600 data frames gathered by the perception loop,
  // oldest first:
  //   {
  //     "frames": [
  //       {"timestamp": <int64>, "cpuTemperatureCelsius": <double>}, ...
  //     ]
  //   }
  http_server_.Get("/api/server/system-health",
      [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            nlohmann::json{
                {"frames", system_health_data_manager_.GetLastFrames(600)}}
                .dump(),
            "application/json");
      });

  // POST /api/server/stop
  // Gracefully stops the Jiaolong Server. The acknowledgement is sent back to
  // the client before the listening socket is closed; after a short delay the
  // HTTP server is stopped so that Start()/listen() returns and the process
  // exits normally. The delay gives the handler time to flush the response.
  http_server_.Post("/api/server/stop", [this](const httplib::Request&,
                                               httplib::Response& res) {
    res.set_content(
        nlohmann::json{{"message", "Jiaolong Server is stopping."}}.dump(),
        "application/json");
    std::thread([this]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      http_server_.stop();
    }).detach();
  });
}

bool JiaolongServer::LaunchAgentSubprocess(const std::string& task_id,
    bool resume) {
  const pid_t agent_pid = fork();
  if (agent_pid < 0) {
    std::cerr << "Error: failed to fork agent subprocess: "
              << std::strerror(errno) << std::endl;
    return false;
  }

  if (agent_pid == 0) {
    // Detach from the server's controlling terminal and ignore stdin so the
    // non-interactive agent never reads from the server's terminal.
    setsid();
    const int dev_null = open("/dev/null", O_RDONLY);
    if (dev_null >= 0) {
      dup2(dev_null, STDIN_FILENO);
      close(dev_null);
    }

    // Build the sub-process arguments. `--resume` is appended only when the
    // client asked the agent to resume previous work on the task.
    std::vector<std::string> args = {
      jiaolong_cli_path_, "--non_interactive", "--task_id", task_id
    };
    if (resume) {
      args.push_back("--resume");
    }
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (std::string& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    execv(jiaolong_cli_path_.c_str(), argv.data());

    // execv returns only when it fails.
    std::cerr << "Error: failed to launch agent subprocess: " << jiaolong_cli_path_
              << ": " << std::strerror(errno) << std::endl;
    _exit(127);
  }

  // Reap the agent subprocess in a background thread so the HTTP server is
  // not blocked. Once the agent finishes working, update the task according
  // to whether the LLM stopped naturally: the CLI exits with status 0 when
  // the LLM stopped (clear agent_working and move the task to needs_review)
  // and with a non-zero status otherwise (clear agent_working and move the
  // task to failed). When the task completed successfully it is then uploaded
  // to GitHub automatically: the commit is pushed and a pull request is
  // opened, which used to require a manual "Upload Commit" action on the task
  // detail page.
  std::thread([this, agent_pid, task_id]() {
    int wait_status = 0;
    pid_t waited_pid;
    do {
      waited_pid = waitpid(agent_pid, &wait_status, 0);
    } while (waited_pid < 0 && errno == EINTR);
    const bool llm_stopped =
        WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0;
    const TaskServiceResult result = llm_stopped
        ? task_service_.CompleteWork(task_id)
        : task_service_.FailWork(task_id);
    if (!result.success) {
      std::cerr << "Error: failed to mark task as finished: "
                << result.error_message << std::endl;
      return;
    }
    if (!llm_stopped) {
      return;
    }
    // The task completed successfully and is now in needs_review: upload the
    // commit to GitHub and open a pull request automatically. A failed upload
    // is logged but does not change the task state, so the task stays
    // available for review.
    const std::optional<Task> task = task_database_.GetTask(task_id);
    if (!task.has_value()) {
      std::cerr << "Error: failed to read task for automatic commit upload: "
                << task_id << std::endl;
      return;
    }
    const auto status_and_output = tool_vcs_upload_commit_.UploadCommit(
        ToolVcsUploadCommitInput(task->id, task->working_directory));
    if (status_and_output.first->type_ != ToolUseStatusType::kSuccess) {
      std::cerr << "Error: failed to automatically upload commit for task "
                << task_id << ": " << status_and_output.second.stdout_output_
                << std::endl;
    }
  }).detach();

  return true;
}

bool JiaolongServer::Start(int port) {
  // Bind to all interfaces so thin clients can reach the server remotely.
  return http_server_.listen("0.0.0.0", port);
}

}  // namespace server
}  // namespace jiaolong