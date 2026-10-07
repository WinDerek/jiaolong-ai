#include <iostream>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>
#include <string>

#include <sqlite3.h>

#include <ftxui/ftxui.hpp>
#include <nlohmann/json.hpp>

#include "agent/jiaolong_agent.h"
#include "agent/project.h"
#include "agent/task.h"

namespace jiaolong {

namespace cli {

enum AgentMode {
  // The user chat with the agent, each round the user gives a task, and the agente work on it.
  kInteractive,
  // The user cannot interactive with the agent. The agent work on tasks from a task source.
  kNonInteractive
};

}  // namespace cli

}  // namespace jiaolong

namespace {

// Reads the Jiaolong settings file (`~/.jiaolong/settings.json`) and returns
// the parsed JSON object. Returns std::nullopt (after logging the error to
// stderr) when HOME is not set, the file cannot be opened, or its content is
// not valid JSON.
std::optional<nlohmann::json> LoadSettings() {
  const char* home_directory = std::getenv("HOME");
  if (home_directory == nullptr) {
    std::cerr << "Error: Failed to read ~/.jiaolong/settings.json: HOME is not set."
              << std::endl;
    return std::nullopt;
  }

  const std::filesystem::path settings_file_path =
      std::filesystem::path(home_directory) / ".jiaolong" / "settings.json";
  std::ifstream settings_file(settings_file_path);
  if (!settings_file.is_open()) {
    std::cerr << "Error: Failed to open settings file: "
              << settings_file_path.string() << std::endl;
    return std::nullopt;
  }

  std::stringstream buffer;
  buffer << settings_file.rdbuf();
  nlohmann::json settings = nlohmann::json::parse(buffer.str(), nullptr, false);
  if (settings.is_discarded()) {
    std::cerr << "Error: Failed to parse settings file: "
              << settings_file_path.string() << std::endl;
    return std::nullopt;
  }
  return settings;
}

// Parses the whitelist of allowed bash commands from the settings file. The
// `allowedBashCommands` field is an array of JSON objects, each containing two
// keys: `bashCommand` and `workingDirectory`. When the field is absent or is
// not an array, an empty whitelist is returned (no bash command is allowed).
// Returns std::nullopt (after logging the error to stderr) when an entry is
// malformed.
std::optional<std::vector<jiaolong::AllowedBashCommand>>
ParseAllowedBashCommands(const nlohmann::json& settings) {
  std::vector<jiaolong::AllowedBashCommand> allowed_bash_commands;
  if (!settings.contains("allowedBashCommands") ||
      !settings["allowedBashCommands"].is_array()) {
    return allowed_bash_commands;
  }
  for (const auto& allowed_command_json : settings["allowedBashCommands"]) {
    if (!allowed_command_json.is_object() ||
        !allowed_command_json.contains("bashCommand") ||
        !allowed_command_json["bashCommand"].is_string() ||
        !allowed_command_json.contains("workingDirectory") ||
        !allowed_command_json["workingDirectory"].is_string()) {
      std::cerr << "Error: allowedBashCommands must be an array of objects, "
                   "each containing string fields \"bashCommand\" and "
                   "\"workingDirectory\"." << std::endl;
      return std::nullopt;
    }
    allowed_bash_commands.push_back(jiaolong::AllowedBashCommand{
        allowed_command_json["bashCommand"].get<std::string>(),
        allowed_command_json["workingDirectory"].get<std::string>()});
  }
  return allowed_bash_commands;
}

// Parses and preprocesses the list of files that the agent is forbidden to
// write to from the settings file. The `forbiddenToWriteFileList` field is an
// array of file paths in the same file system the agent runs in. Because the
// Agent only supports real file paths (virtual paths are no longer supported),
// every entry is validated here before the Agent is constructed, and an entry
// is kept only when it is a real file path:
//
//   - it must be an absolute path, and
//   - it must point to an existing regular file (a directory or a non-existent
//     path is not a file).
//
// Any entry that fails validation (a non-string item, a relative path, a
// directory, a non-existent path, ...) is discarded gracefully: a warning is
// logged to stderr and the entry is skipped, so a single bad entry never
// invalidates the whole list. When the field is absent or is not an array, an
// empty list is returned (nothing is forbidden). The preprocessing lives here
// in the CLI so the Agent always receives a well-formed, mandatory list of real
// file paths.
std::vector<std::string> ParseForbiddenToWriteFileList(
    const nlohmann::json& settings) {
  std::vector<std::string> forbidden_to_write_file_list;
  if (!settings.contains("forbiddenToWriteFileList") ||
      !settings["forbiddenToWriteFileList"].is_array()) {
    return forbidden_to_write_file_list;
  }
  for (const auto& forbidden_file_json : settings["forbiddenToWriteFileList"]) {
    if (!forbidden_file_json.is_string()) {
      std::cerr << "Warning: ignoring a non-string entry in "
                   "forbiddenToWriteFileList." << std::endl;
      continue;
    }
    const std::string forbidden_file_path =
        forbidden_file_json.get<std::string>();
    const std::filesystem::path forbidden_file_system_path(forbidden_file_path);
    // A valid entry must be an absolute real path. Virtual paths such as
    // `/workspace/foo.txt` are not absolute in the host file system the agent
    // runs in, so they are discarded here.
    if (!forbidden_file_system_path.is_absolute()) {
      std::cerr << "Warning: ignoring forbiddenToWriteFileList entry that is "
                   "not an absolute path: " << forbidden_file_path << std::endl;
      continue;
    }
    // A valid entry must point to a real file; directories, symlinks to
    // directories and non-existent paths are all discarded.
    std::error_code error_code;
    if (!std::filesystem::is_regular_file(forbidden_file_system_path,
            error_code)) {
      std::cerr << "Warning: ignoring forbiddenToWriteFileList entry that is "
                   "not a file: " << forbidden_file_path << std::endl;
      continue;
    }
    forbidden_to_write_file_list.push_back(forbidden_file_path);
  }
  return forbidden_to_write_file_list;
}

// Returns the default metadata database path: ~/.jiaolong/database.sqlite.
// Returns std::nullopt when HOME is not set.
std::optional<std::string> DefaultDatabasePath() {
  const char* home_directory = std::getenv("HOME");
  if (home_directory == nullptr) {
    std::cerr << "Error: Failed to locate database: HOME is not set."
              << std::endl;
    return std::nullopt;
  }
  return (std::filesystem::path(home_directory) /
      ".jiaolong" / "database.sqlite").string();
}

// Returns true when the given table has a column with the given name.
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

// Returns true when a table with the given name exists in the database.
bool TableExists(sqlite3* db, const std::string& table_name) {
  sqlite3_stmt* stmt = nullptr;
  static const char kSelectTable[] =
      "SELECT name FROM sqlite_master WHERE type = 'table' AND name = ?;";
  if (sqlite3_prepare_v2(db, kSelectTable, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, table_name.c_str(), -1, SQLITE_TRANSIENT);
  const bool exists = sqlite3_step(stmt) == SQLITE_ROW;
  sqlite3_finalize(stmt);
  return exists;
}

// Splits a comma-separated list of ids into a vector, dropping empty entries.
std::vector<std::string> SplitCommaSeparated(const std::string& value) {
  std::vector<std::string> parts;
  std::stringstream stream(value);
  std::string part;
  while (std::getline(stream, part, ',')) {
    if (!part.empty()) {
      parts.push_back(part);
    }
  }
  return parts;
}

// A task loaded from the metadata database together with the lightweight
// project (readonly-directory catalog) the agent needs to build its mount
// table.
struct LoadedTask {
  jiaolong::Task task;
  jiaolong::Project project;
};

// Loads the readonly-directory catalog of the given project from the metadata
// database. Returns an empty catalog when the project id is empty or the
// catalog table does not exist (older databases), so the loader never creates
// or migrates schema.
jiaolong::Project LoadProjectFromDatabase(sqlite3* db,
    const std::string& project_id) {
  jiaolong::Project project;
  project.id_ = project_id;
  if (project_id.empty() ||
      !TableExists(db, "project_readonly_directories")) {
    return project;
  }
  sqlite3_stmt* stmt = nullptr;
  static const char kSelectReadonlyDirectories[] =
      "SELECT id, alias, real_path, description"
      "  FROM project_readonly_directories WHERE project_id = ?"
      "  ORDER BY ordering ASC, id ASC;";
  if (sqlite3_prepare_v2(db, kSelectReadonlyDirectories, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return project;
  }
  sqlite3_bind_text(stmt, 1, project_id.c_str(), -1, SQLITE_TRANSIENT);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    jiaolong::ReadonlyDirectory directory;
    const unsigned char* id_text = sqlite3_column_text(stmt, 0);
    const unsigned char* alias_text = sqlite3_column_text(stmt, 1);
    const unsigned char* real_path_text = sqlite3_column_text(stmt, 2);
    const unsigned char* description_text = sqlite3_column_text(stmt, 3);
    directory.id_ =
        id_text != nullptr ? reinterpret_cast<const char*>(id_text) : "";
    directory.alias_ =
        alias_text != nullptr ? reinterpret_cast<const char*>(alias_text) : "";
    directory.real_path_ = real_path_text != nullptr
        ? reinterpret_cast<const char*>(real_path_text) : "";
    directory.description_ = description_text != nullptr
        ? reinterpret_cast<const char*>(description_text) : "";
    project.readonly_directories_.push_back(directory);
  }
  sqlite3_finalize(stmt);
  return project;
}

// Loads a task from the Jiaolong metadata database by task id. This is the
// CLI's own lightweight database reader and deliberately does not depend on
// any Jiaolong Server code; it talks to sqlite directly through sqlite_lib.
//
// The database is expected to already exist (Jiaolong Server creates it on
// boot). If it does not exist, or the task cannot be found, this function
// logs an error to stderr and returns std::nullopt.
std::optional<LoadedTask> LoadTaskFromDatabase(const std::string& task_id) {
  const std::optional<std::string> db_path_opt = DefaultDatabasePath();
  if (!db_path_opt.has_value()) {
    return std::nullopt;
  }
  const std::string db_path = *db_path_opt;
  if (!std::filesystem::exists(db_path)) {
    std::cerr << "Error: Jiaolong database does not exist: " << db_path
              << std::endl;
    return std::nullopt;
  }

  sqlite3* db = nullptr;
  if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
    const std::string error_message =
        db != nullptr ? sqlite3_errmsg(db) : "unknown error";
    if (db != nullptr) {
      sqlite3_close(db);
    }
    std::cerr << "Error: Failed to open Jiaolong database at " << db_path
              << ": " << error_message << std::endl;
    return std::nullopt;
  }

  // The tasks table is expected to already carry the readonly_directories and
  // project_id columns (see the design doc's manual schema update). For
  // robustness against older databases the loader detects the columns and
  // falls back to an empty selection / project when they are missing, without
  // ever creating or migrating schema.
  const bool has_readonly_directories =
      TableHasColumn(db, "tasks", "readonly_directories");
  const bool has_project_id = TableHasColumn(db, "tasks", "project_id");
  std::string select_task =
      "SELECT title, description, working_directory, token_limit, ";
  select_task += has_readonly_directories ? "readonly_directories" : "''";
  select_task += ", ";
  select_task += has_project_id ? "project_id" : "''";
  select_task += " FROM tasks WHERE id = ?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, select_task.c_str(), -1, &stmt, nullptr) !=
      SQLITE_OK) {
    const std::string error_message = sqlite3_errmsg(db);
    sqlite3_close(db);
    std::cerr << "Error: Failed to query Jiaolong database: " << error_message
              << std::endl;
    return std::nullopt;
  }

  sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);

  LoadedTask loaded_task;
  std::string project_id;
  const int step_result = sqlite3_step(stmt);
  if (step_result == SQLITE_ROW) {
    const unsigned char* title_text = sqlite3_column_text(stmt, 0);
    const unsigned char* description_text = sqlite3_column_text(stmt, 1);
    const unsigned char* working_directory_text = sqlite3_column_text(stmt, 2);
    const unsigned char* readonly_directories_text =
        sqlite3_column_text(stmt, 4);
    const unsigned char* project_id_text = sqlite3_column_text(stmt, 5);
    const std::string readonly_directories = readonly_directories_text != nullptr
        ? reinterpret_cast<const char*>(readonly_directories_text) : "";
    project_id = project_id_text != nullptr
        ? reinterpret_cast<const char*>(project_id_text) : "";
    loaded_task.task = jiaolong::Task(
        task_id,
        title_text != nullptr
            ? reinterpret_cast<const char*>(title_text) : "",
        description_text != nullptr
            ? reinterpret_cast<const char*>(description_text) : "",
        working_directory_text != nullptr
            ? reinterpret_cast<const char*>(working_directory_text) : "",
        sqlite3_column_int(stmt, 3));
    loaded_task.task.readonly_directories_ =
        SplitCommaSeparated(readonly_directories);
  }
  sqlite3_finalize(stmt);

  if (step_result != SQLITE_ROW) {
    sqlite3_close(db);
    std::cerr << "Error: Task not found in database: " << task_id << std::endl;
    return std::nullopt;
  }

  // Join the task's selection with the project's readonly-directory catalog.
  loaded_task.project = LoadProjectFromDatabase(db, project_id);
  sqlite3_close(db);

  return loaded_task;
}

// Updates the total_token_usage column of the task with the given id in the
// Jiaolong metadata database. This is called by the agent itself after a
// successful run so the tokens consumed while working on the task are saved
// and can be reviewed later. The column is expected to exist because the
// database schema is kept aligned with doc/database_design.md manually: neither
// the server nor the CLI creates or migrates schema at runtime. When the update
// fails an error is logged to stderr and false is returned.
bool UpdateTaskTotalTokenUsageInDatabase(const std::string& task_id,
    int total_token_usage) {
  const std::optional<std::string> db_path_opt = DefaultDatabasePath();
  if (!db_path_opt.has_value()) {
    return false;
  }
  const std::string db_path = *db_path_opt;
  if (!std::filesystem::exists(db_path)) {
    std::cerr << "Error: Jiaolong database does not exist: " << db_path
              << std::endl;
    return false;
  }

  sqlite3* db = nullptr;
  if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
    const std::string error_message =
        db != nullptr ? sqlite3_errmsg(db) : "unknown error";
    if (db != nullptr) {
      sqlite3_close(db);
    }
    std::cerr << "Error: Failed to open Jiaolong database at " << db_path
              << ": " << error_message << std::endl;
    return false;
  }

  sqlite3_stmt* stmt = nullptr;
  static const char kUpdateTaskTotalTokenUsage[] =
      "UPDATE tasks SET total_token_usage = ? WHERE id = ?;";
  if (sqlite3_prepare_v2(db, kUpdateTaskTotalTokenUsage, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    const std::string error_message = sqlite3_errmsg(db);
    sqlite3_close(db);
    std::cerr << "Error: Failed to prepare total token usage update: "
              << error_message << std::endl;
    return false;
  }

  sqlite3_bind_int(stmt, 1, total_token_usage);
  sqlite3_bind_text(stmt, 2, task_id.c_str(), -1, SQLITE_TRANSIENT);

  const int step_result = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  const int changed_rows = sqlite3_changes(db);
  sqlite3_close(db);

  if (step_result != SQLITE_DONE || changed_rows <= 0) {
    std::cerr << "Error: Failed to update total token usage for task: "
              << task_id << std::endl;
    return false;
  }
  return true;
}

// Maps a WorkFailureReason to the value stored in the tasks.failure_reason
// column. A failed run always maps to a non-empty value: kNone (which is not
// reached for a failed run) and kExecutionFailure both fall back to
// "execution_failure" so the CLI never leaves a failure unlabeled.
std::string FailureReasonToDatabaseValue(jiaolong::WorkFailureReason reason) {
  switch (reason) {
    case jiaolong::WorkFailureReason::kTokenNotEnough:
      return "token_not_enough";
    case jiaolong::WorkFailureReason::kExecutionFailure:
    case jiaolong::WorkFailureReason::kNone:
    default:
      return "execution_failure";
  }
}

// Updates the failure_reason column of the task with the given id in the
// Jiaolong metadata database. It is called by the CLI when a non-interactive
// run fails, so the client can show why the task failed. Like the total token
// usage write, the column is expected to already exist because the database
// schema is kept aligned with doc/database_design.md manually: neither the
// server nor the CLI creates or migrates schema at runtime. When the update
// fails an error is logged to stderr and false is returned (the server's
// FailWork then records a generic execution_failure), so a missing column
// never crashes the CLI.
bool UpdateTaskFailureReasonInDatabase(const std::string& task_id,
    const std::string& failure_reason) {
  const std::optional<std::string> db_path_opt = DefaultDatabasePath();
  if (!db_path_opt.has_value()) {
    return false;
  }
  const std::string db_path = *db_path_opt;
  if (!std::filesystem::exists(db_path)) {
    std::cerr << "Error: Jiaolong database does not exist: " << db_path
              << std::endl;
    return false;
  }

  sqlite3* db = nullptr;
  if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
    const std::string error_message =
        db != nullptr ? sqlite3_errmsg(db) : "unknown error";
    if (db != nullptr) {
      sqlite3_close(db);
    }
    std::cerr << "Error: Failed to open Jiaolong database at " << db_path
              << ": " << error_message << std::endl;
    return false;
  }

  sqlite3_stmt* stmt = nullptr;
  static const char kUpdateTaskFailureReason[] =
      "UPDATE tasks SET failure_reason = ? WHERE id = ?;";
  if (sqlite3_prepare_v2(db, kUpdateTaskFailureReason, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    const std::string error_message = sqlite3_errmsg(db);
    sqlite3_close(db);
    std::cerr << "Error: Failed to prepare failure reason update: "
              << error_message << std::endl;
    return false;
  }

  sqlite3_bind_text(stmt, 1, failure_reason.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, task_id.c_str(), -1, SQLITE_TRANSIENT);

  const int step_result = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  const int changed_rows = sqlite3_changes(db);
  sqlite3_close(db);

  if (step_result != SQLITE_DONE || changed_rows <= 0) {
    std::cerr << "Error: Failed to update failure reason for task: "
              << task_id << std::endl;
    return false;
  }
  return true;
}

// Command line options parsed from argv.
struct CliOptions {
  bool non_interactive = false;
  // Whether the agent should resume previous work on the task from the
  // persisted session history (`~/.jiaolong/sessions`) instead of starting a
  // fresh round.
  bool resume = false;
  // At most one of task_id / task_file_path may be set.
  std::string task_id;
  std::string task_file_path;
};

// Parses the jiaolong_cli command line. Flags may appear in any order and
// accept both `--flag value` and `--flag=value` forms. On invalid or unknown
// input an error is printed to stderr and false is returned.
bool ParseCommandLineOptions(int argc, char* argv[], CliOptions& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--non_interactive") {
      options.non_interactive = true;
      continue;
    }

    if (arg == "--resume") {
      options.resume = true;
      continue;
    }

    std::string flag = arg;
    std::string value;
    const bool has_inline_value =
        arg.rfind("--", 0) == 0 && arg.find('=') != std::string::npos;
    if (has_inline_value) {
      const size_t equals_pos = arg.find('=');
      flag = arg.substr(0, equals_pos);
      value = arg.substr(equals_pos + 1);
    }

    auto take_value = [&](const std::string& flag_name) -> bool {
      if (has_inline_value) {
        if (value.empty()) {
          std::cerr << "Error: " << flag_name
                    << " requires a non-empty value." << std::endl;
          return false;
        }
        return true;
      }
      if (i + 1 >= argc) {
        std::cerr << "Error: " << flag_name << " requires a value." << std::endl;
        return false;
      }
      value = argv[++i];
      return true;
    };

    if (flag == "--task_id") {
      if (!take_value("--task_id")) {
        return false;
      }
      options.task_id = value;
    } else if (flag == "--resume") {
      // Boolean flag. The plain `--resume` form was handled above; here we
      // accept the inline `--resume=true` / `--resume=false` forms as well.
      if (value == "true" || value == "1") {
        options.resume = true;
      } else if (value == "false" || value == "0") {
        options.resume = false;
      } else {
        std::cerr << "Error: --resume expects a boolean value (true or "
                     "false)." << std::endl;
        return false;
      }
    } else if (flag == "--task_file") {
      if (!take_value("--task_file")) {
        return false;
      }
      options.task_file_path = value;
    } else {
      std::cerr << "Error: unknown command line argument: " << arg << std::endl;
      return false;
    }
  }
  return true;
}

// Reads a task description from the user through an FTXUI-based TUI. The text
// typed by the user (including slash commands such as `/exit`) is returned.
std::string ReadTaskInputFromUser() {
  using namespace ftxui;

  std::string user_input;

  auto screen = App::TerminalOutput();
  auto exit_loop = screen.ExitLoopClosure();

  // Pressing Enter in the input box or clicking "Submit" submits the task and
  // exits the TUI loop.
  InputOption input_option;
  input_option.multiline = false;
  input_option.on_enter = exit_loop;

  Component input =
      Input(&user_input, "Describe a task for the agent...", input_option);
  Component submit_button = Button("Submit", exit_loop);

  auto component = Container::Vertical({
      input,
      submit_button,
  });

  auto renderer = Renderer(component, [&] {
    return vbox({
               text("Jiaolong CLI - Interactive Mode") | bold | center,
               separator(),
               text("Enter a task for the agent. Type /clear to clear the "
                    "conversation, /exit or /quit to quit.") | dim,
               input->Render() | border,
               hbox({
                   submit_button->Render(),
                   filler(),
               }),
           }) |
           border;
  });

  screen.Loop(renderer);
  return user_input;
}

void RunAgentInteractively(jiaolong::Agent& jiaolong_agent) {
  // Slash commands
  const char* kSlashCommandClear = "/clear";
  const char* kSlashCommandExit = "/exit";
  const char* kSlashCommandQuit = "/quit";

  // Set agent tool permission.
  jiaolong_agent.SetToolPermission(jiaolong::ToolPermission::kAsk);

  std::string user_input;
  while (true) {
    // Read user input through the FTXUI-based TUI.
    user_input = ReadTaskInputFromUser();

    if (user_input == kSlashCommandClear) {
      jiaolong_agent.AppendSlashCommand(kSlashCommandClear);
      jiaolong_agent.ClearMessages();
    } else if (user_input == kSlashCommandExit ||
        user_input == kSlashCommandQuit) {
      jiaolong_agent.AppendSlashCommand(user_input);
      break;
    } else {
      // Treat input as a task.
      jiaolong::Task task(/*id=*/ "", /*title=*/ "", /*description=*/ user_input,
          /*working_directory=*/ "/home/user/workspace/do_not_track/jiaolong_test",
          /*token_limit=*/ 1000000);
      jiaolong_agent.Work(task);
    }
  }
}

// Runs the agent on the task in non-interactive mode. When `resume` is true
// the agent resumes previous work on the task from the persisted session
// history. Returns true when the LLM stopped naturally (the task is ready for
// review), false otherwise (the task should be marked as failed).
bool RunAgentNonInteractively(jiaolong::Agent& jiaolong_agent,
    const jiaolong::Task& task, const jiaolong::Project& project, bool resume) {
  // Allow all tool calls for non-interactive mode.
  jiaolong_agent.SetToolPermission(jiaolong::ToolPermission::kAllowAll);
  return jiaolong_agent.Work(task, project, resume);
}

}  // namespace

int main(int argc, char* argv[]) {
  std::cout << "=== Welcome to Jiaolong CLI! ===" << std::endl;

  // Parse command line arguments. Unknown flags, malformed values and
  // conflicting task sources fail fast with an error printed to stderr.
  CliOptions options;
  if (!ParseCommandLineOptions(argc, argv, options)) {
    std::cerr << "Usage: jiaolong_cli [--non_interactive] [--resume]"
              << " [--task_id <task-id> | --task_file <path>]" << std::endl;
    return 1;
  }

  // `--task_id` and `--task_file` are the two acceptable task sources in
  // non-interactive mode, but they cannot be set at the same time.
  if (!options.task_id.empty() && !options.task_file_path.empty()) {
    std::cerr << "Error: --task_id and --task_file cannot be set at the same "
                 "time." << std::endl;
    return 1;
  }
  if (options.non_interactive && options.task_id.empty() &&
      options.task_file_path.empty()) {
    std::cerr << "Error: non-interactive mode requires either --task_id or "
                 "--task_file." << std::endl;
    return 1;
  }
  if (!options.non_interactive &&
      (!options.task_id.empty() || !options.task_file_path.empty() ||
       options.resume)) {
    std::cerr << "Error: --task_id, --task_file and --resume are only "
                 "supported in non-interactive mode (--non_interactive)."
              << std::endl;
    return 1;
  }

  const jiaolong::cli::AgentMode agent_mode = options.non_interactive
      ? jiaolong::cli::AgentMode::kNonInteractive
      : jiaolong::cli::AgentMode::kInteractive;
  std::cout << "Mode: "
            << (agent_mode == jiaolong::cli::AgentMode::kInteractive
                    ? "interactive" : "non-interactive")
            << std::endl;

  // When running non-interactively, load the task before creating the agent so
  // an invalid task source fails fast with an error printed to stderr. The
  // task is read either from a JSON file (--task_file) or directly from the
  // Jiaolong metadata database (--task_id).
  jiaolong::Task task;
  jiaolong::Project project;
  if (agent_mode == jiaolong::cli::AgentMode::kNonInteractive) {
    if (!options.task_file_path.empty()) {
      std::cout << "task file path: " << options.task_file_path << std::endl;
      std::ifstream task_file(options.task_file_path);
      if (!task_file.is_open()) {
        std::cerr << "failed to read task file: "
                  << options.task_file_path << std::endl;
        return 1;
      }
      std::stringstream task_file_buffer;
      task_file_buffer << task_file.rdbuf();
      nlohmann::json task_json =
          nlohmann::json::parse(task_file_buffer.str(), nullptr, false);
      if (task_json.is_discarded()) {
        std::cerr << "failed to parse task file: "
                  << options.task_file_path << std::endl;
        return 1;
      }
      const std::optional<jiaolong::Task> parsed_task =
          jiaolong::Task::FromJson(task_json);
      if (!parsed_task.has_value()) {
        std::cerr << "failed to parse task file (missing or invalid fields): "
                  << options.task_file_path << std::endl;
        return 1;
      }
      task = *parsed_task;
      // The task file may also carry the project's optional
      // `readonlyDirectories` catalog (and/or an `id`).
      const std::optional<jiaolong::Project> parsed_project =
          jiaolong::Project::FromJson(task_json);
      if (!parsed_project.has_value()) {
        std::cerr << "failed to parse project (readonlyDirectories) in task "
                     "file: "
                  << options.task_file_path << std::endl;
        return 1;
      }
      project = *parsed_project;
    } else {
      std::cout << "task id: " << options.task_id << std::endl;
      const std::optional<LoadedTask> loaded_task =
          LoadTaskFromDatabase(options.task_id);
      if (!loaded_task.has_value()) {
        return 1;
      }
      task = loaded_task->task;
      project = loaded_task->project;
    }
  }

  // Create the Jiaolong agent. LLM provider settings (base URL, security key,
  // model, LLM cooldown duration and retry times) are read from
  // ~/.jiaolong/settings.json. The enabled LLM provider is selected with the
  // `enabledLlmProviderIndex` setting (an index into the `llmProviders`
  // array). For backward compatibility, when that field is absent or out of
  // range, the first provider whose `enabled` field is true is used instead.
  // If the settings file does not exist, or the JSON format is incorrect, exit
  // immediately with an error message to stderr instead of crashing.
  const std::optional<nlohmann::json> settings_opt = LoadSettings();
  if (!settings_opt.has_value()) {
    return 1;
  }
  const nlohmann::json& settings = *settings_opt;

  if (!settings.contains("llmProviders") ||
      !settings["llmProviders"].is_array()) {
    std::cerr << "Error: llmProviders must be an array." << std::endl;
    return 1;
  }
  const nlohmann::json& llm_providers = settings["llmProviders"];

  const nlohmann::json* selected_llm_provider = nullptr;
  if (settings.contains("enabledLlmProviderIndex") &&
      settings["enabledLlmProviderIndex"].is_number_integer()) {
    const int enabled_llm_provider_index =
        settings["enabledLlmProviderIndex"].get<int>();
    if (enabled_llm_provider_index >= 0 &&
        enabled_llm_provider_index < static_cast<int>(llm_providers.size())) {
      selected_llm_provider = &llm_providers[enabled_llm_provider_index];
    }
  }
  if (selected_llm_provider == nullptr) {
    for (const auto& llm_provider : llm_providers) {
      if (llm_provider.value("enabled", false)) {
        selected_llm_provider = &llm_provider;
        break;
      }
    }
  }
  if (selected_llm_provider == nullptr) {
    std::cerr << "Error: No enabled LLM provider found in llmProviders."
              << std::endl;
    return 1;
  }

  // The selected provider must carry the required connection fields.
  if (!selected_llm_provider->contains("baseUrl") ||
      !(*selected_llm_provider)["baseUrl"].is_string() ||
      !selected_llm_provider->contains("model") ||
      !(*selected_llm_provider)["model"].is_string() ||
      !selected_llm_provider->contains("securityKey") ||
      !(*selected_llm_provider)["securityKey"].is_string()) {
    std::cerr << "Error: enabled LLM provider must contain string fields "
                 "baseUrl, model and securityKey." << std::endl;
    return 1;
  }
  const std::string llm_api_base_url =
      (*selected_llm_provider)["baseUrl"].get<std::string>();
  const std::string llm_model =
      (*selected_llm_provider)["model"].get<std::string>();
  const std::string security_key =
      (*selected_llm_provider)["securityKey"].get<std::string>();
  const int llm_cooldown_duration =
      selected_llm_provider->value("llmCooldownDuration", 0);
  // Number of times to retry the LLM API call after a non-successful
  // response. Each LLM provider may configure a different value. When it is
  // not set, it defaults to 0 (no retry).
  const int llm_retry_times =
      selected_llm_provider->value("retryTimes", 0);

  // The path to the Jujutsu (jj) executable used by the VCS tools. It is
  // configured in the settings file under `jjExecutablePath`; when absent,
  // it defaults to `jj` (assumed to be on PATH).
  const std::string jj_executable_path =
      settings.value("jjExecutablePath", "jj");

  // The path to the GitHub CLI (gh) executable used by the VCS tools. It is
  // configured in the settings file under `ghExecutablePath`; when absent,
  // it defaults to `gh` (assumed to be on PATH).
  const std::string github_token = settings.value("githubToken", "test_token");
  const std::string gh_executable_path =
      settings.value("ghExecutablePath", "gh");

  // The SOCKS proxy address used by the VCS tools for remote operations
  // (`jj git fetch`, `jj git push`). It is configured in the settings file
  // under `socksProxy`; when absent, a warning is printed to stdout and the
  // default value `127.0.0.1:7891` is used.
  std::string socks_proxy = settings.value("socksProxy", "");
  if (socks_proxy.empty()) {
    std::cout << "Warning: socksProxy is not set, using default value "
                 "127.0.0.1:7891" << std::endl;
    socks_proxy = "127.0.0.1:7891";
  }

  // The whitelist of bash commands that the Bash tool is allowed to
  // execute. It is configured in the settings file under
  // `allowedBashCommands` (an array of objects, each with `bashCommand` and
  // `workingDirectory` keys); when absent, no bash commands are allowed.
  const std::optional<std::vector<jiaolong::AllowedBashCommand>>
      allowed_bash_commands_opt = ParseAllowedBashCommands(settings);
  if (!allowed_bash_commands_opt.has_value()) {
    return 1;
  }
  const std::vector<jiaolong::AllowedBashCommand> allowed_bash_commands =
      *allowed_bash_commands_opt;

  // The list of real file paths the agent is forbidden to modify. It is
  // configured in the settings file under `forbiddenToWriteFileList` (an array
  // of paths in the agent's own file system) and preprocessed by
  // ParseForbiddenToWriteFileList, which discards every entry that is not an
  // absolute path pointing to an existing regular file. When the field is
  // absent or malformed the list is empty, i.e. nothing is forbidden.
  const std::vector<std::string> forbidden_to_write_file_list =
      ParseForbiddenToWriteFileList(settings);

  jiaolong::Agent jiaolong_agent(llm_api_base_url, llm_model, security_key,
      llm_cooldown_duration, llm_retry_times, jj_executable_path, github_token,
      gh_executable_path, allowed_bash_commands, socks_proxy,
      forbidden_to_write_file_list);

  switch (agent_mode) {
    case jiaolong::cli::AgentMode::kInteractive: {
      RunAgentInteractively(jiaolong_agent);
      break;
    }
    case jiaolong::cli::AgentMode::kNonInteractive: {
      // Exit with status 0 when the LLM stopped naturally (the server marks
      // the task as needs_review); exit with status 1 otherwise (the server
      // marks the task as failed).
      const bool llm_stopped = RunAgentNonInteractively(
          jiaolong_agent, task, project, options.resume);
      // Persist the run result into the task row (already the pattern for
      // token usage). The total token usage is written on every terminal case
      // so the UI can also show how close a token-exhausted task got to its
      // limit; a failure reason is written when the run did not stop naturally
      // because the CLI is the only component that knows why it failed.
      UpdateTaskTotalTokenUsageInDatabase(task.id_,
          jiaolong_agent.total_token_usage());
      if (!llm_stopped) {
        UpdateTaskFailureReasonInDatabase(
            task.id_,
            FailureReasonToDatabaseValue(jiaolong_agent.failure_reason()));
      }
      return llm_stopped ? 0 : 1;
    }
  }

  return 0;
}