#pragma once

#include <string>

#include <httplib.h>

#include "agent/tool/tool_vcs.h"
#include "server/client_credential_manager.h"
#include "server/project_database.h"
#include "server/project_service.h"
#include "server/settings_service.h"
#include "server/system_health_data_manager.h"
#include "server/task_database.h"
#include "server/task_service.h"

namespace jiaolong {
namespace server {

class JiaolongServer {
 public:
  // Constructing the server opens the default sqlite database at
  // ~/.jiaolong/database.sqlite. The database and its schema are expected to
  // already exist (see doc/database_design.md); the server never creates or
  // migrates the schema at runtime. `gh_executable_path` is the full
  // path to the GitHub CLI (gh) executable used by the VCS UploadCommit tool;
  // it is read from the settings file (`ghExecutablePath`). `socks_proxy` is
  // the SOCKS proxy address used by the VCS tools for remote operations; it is
  // mandatory and is always set explicitly by the server main.cc, which reads
  // the `socksProxy` setting (applying the default when the field is absent).
  // `mac_system_health_data_server_domain` is the domain of the mac system
  // health data server that the system health perception loop requests; it is
  // mandatory and is always set explicitly by the server main.cc, which reads
  // the `macSystemHealthDataServerDomain` setting (applying the default when
  // the field is absent). `username` and `password` are
  // the client credentials configured in the settings file (keys `username`
  // and `password`); they are passed to the ClientCredentialManager and can
  // never be modified by clients.
  JiaolongServer(const std::string& jiaolong_cli_path,
      const std::string& jj_executable_path,
      const std::string& github_token,
      const std::string& gh_executable_path,
      const std::string& socks_proxy,
      const std::string& mac_system_health_data_server_domain,
      const std::string& username,
      const std::string& password);

  // Stops the system health perception loop (if it is still running).
  ~JiaolongServer();

  // Registers request logging, all task-management REST endpoints and the
  // server lifecycle endpoints.
  void RegisterRoutes();

  // Starts the HTTP server on the given port and blocks until it stops.
  // Returns false if the server could not be started.
  bool Start(int port);

  // Returns the task database used by the task-management REST APIs.
  TaskDatabase& GetTaskDatabase() { return task_database_; }

  // Returns the HTTP server that serves the REST APIs. Exposed so tests can
  // bind it to an ephemeral port and drive requests through the real routing
  // table (see jiaolong_server_test.cc).
  httplib::Server& GetHttpServer() { return http_server_; }

 private:
  // Registers the per-request stdout logger.
  void RegisterLogging();

  // Registers the OAuth 2.0 authentication HTTP APIs (login, refresh, logout)
  // and installs the pre-routing check that protects every other REST API.
  void RegisterAuthRoutes();

  // Registers all task-management HTTP APIs.
  void RegisterTaskManagementRoutes();

  // Registers the project-management HTTP APIs (list, create and update
  // projects, including their readonly-directory catalogs).
  void RegisterProjectRoutes();

  // Registers the settings HTTP APIs (e.g. LLM provider selection).
  void RegisterSettingsRoutes();

  // Registers the server lifecycle HTTP APIs (e.g. graceful shutdown).
  void RegisterServerRoutes();

  // Starts a sub-process that runs the Jiaolong CLI in non-interactive mode
  // (`--non_interactive --task_id <task_id>`, plus `--resume` when `resume` is
  // true) so the agent can work on the given task. When `resume` is true the
  // agent resumes previous work on the task from the persisted session
  // history. The sub-process is reaped in a background thread and, once the
  // agent finishes working, the task is updated accordingly (agent_working is
  // cleared and the task moves to needs_review). When the task completes
  // successfully (the LLM stopped naturally) the task's commit is then
  // uploaded to GitHub automatically (pushed and a pull request opened).
  // Returns false when the sub-process could not be started.
  bool LaunchAgentSubprocess(const std::string& task_id, bool resume);

  const std::string jiaolong_cli_path_;

  // VCS tools. InitBranch backs the "Init Branch & start working" task action;
  // UploadCommit is run automatically by the server when a task execution
  // completes successfully (it is no longer triggered by a client button).
  ToolVcsInitBranch tool_vcs_init_branch_;
  ToolVcsUploadCommit tool_vcs_upload_commit_;

  httplib::Server http_server_;
  TaskDatabase task_database_;
  ProjectDatabase project_database_;
  TaskService task_service_{task_database_, project_database_};
  ProjectService project_service_{project_database_};
  SettingsService settings_service_;
  ClientCredentialManager client_credential_manager_;

  // Gathers server system health data (currently CPU temperature) in a
  // perception loop that runs on its own thread from server boot.
  SystemHealthDataManager system_health_data_manager_;
};

}  // namespace server
}  // namespace jiaolong
