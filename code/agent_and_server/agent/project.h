#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace jiaolong {

// A project-declared readonly directory candidate. A task selects a subset of
// its project's catalog by id and only the selected entries become read-only
// mounts for the task. See doc/technology/multiple_folders_system_design.md.
struct ReadonlyDirectory {
  // Stable catalog id; pointed to by task selections so an alias rename never
  // silently changes what a task mounts.
  std::string id_;
  // Short, validated name used as the virtual root suffix
  // (`/readonly/<alias>`).
  std::string alias_;
  // Absolute host directory the alias maps to.
  std::string real_path_;
  // Human-readable description shown to the LLM in the system prompt.
  std::string description_;
};

// The lightweight project data the agent needs at run time. It carries the
// project id and the readonly-directory catalog that tasks select from. It is
// intentionally independent of the server-side Project so the CLI can build it
// from the metadata database without depending on server code.
class Project {
 public:
  std::string id_;
  std::vector<ReadonlyDirectory> readonly_directories_;

  Project() {}

  Project(const std::string& id,
      const std::vector<ReadonlyDirectory>& readonly_directories)
      : id_(id), readonly_directories_(readonly_directories) {}

  // Parses a project from a JSON object. `readonlyDirectories` is optional and
  // defaults to an empty catalog (backward compatible). When present it must
  // be an array of objects, each carrying string `alias` and `realPath`
  // (optional `id` and `description`); a malformed entry makes parsing fail.
  static std::optional<Project> FromJson(const nlohmann::json& json) {
    Project project;
    if (json.is_object() && json.contains("id") && json["id"].is_string()) {
      project.id_ = json["id"].get<std::string>();
    }
    if (json.is_object() && json.contains("readonlyDirectories")) {
      const nlohmann::json& catalog_json = json["readonlyDirectories"];
      if (!catalog_json.is_array()) {
        return std::nullopt;
      }
      for (const nlohmann::json& entry_json : catalog_json) {
        if (!entry_json.is_object() ||
            !entry_json.contains("alias") || !entry_json["alias"].is_string() ||
            !entry_json.contains("realPath") ||
            !entry_json["realPath"].is_string()) {
          return std::nullopt;
        }
        ReadonlyDirectory directory;
        directory.alias_ = entry_json["alias"].get<std::string>();
        directory.real_path_ = entry_json["realPath"].get<std::string>();
        if (entry_json.contains("id") && entry_json["id"].is_string()) {
          directory.id_ = entry_json["id"].get<std::string>();
        }
        if (entry_json.contains("description") &&
            entry_json["description"].is_string()) {
          directory.description_ = entry_json["description"].get<std::string>();
        }
        project.readonly_directories_.push_back(directory);
      }
    }
    return project;
  }
};

}  // namespace jiaolong