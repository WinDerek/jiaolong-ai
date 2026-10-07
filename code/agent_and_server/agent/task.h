#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace jiaolong {

class Task {

 public:

  std::string id_;
  std::string title_;
  std::string description_;
  std::string working_directory_;
  int token_limit_;
  // Ids of the project's readonly directories this task selected, in mount
  // order. Empty by default, so a task with no selection mounts only
  // `/workspace`. The ids point into the project's readonly-directory catalog
  // (see `agent/project.h`).
  std::vector<std::string> readonly_directories_;

  // Parses a task from a JSON object. Returns std::nullopt when the object is
  // missing any required field or a required field has an unexpected type.
  // `readonlyDirectoryIds` is optional; when present it must be an array of
  // strings. When it is absent (or empty) the task mounts no readonly
  // directories, matching the pre-feature behavior.
  static std::optional<Task> FromJson(const nlohmann::json& json) {
    if (!json.is_object() ||
        !json.contains("id") || !json["id"].is_string() ||
        !json.contains("title") || !json["title"].is_string() ||
        !json.contains("description") || !json["description"].is_string() ||
        !json.contains("workingDirectory") ||
        !json["workingDirectory"].is_string() ||
        !json.contains("tokenLimit") || !json["tokenLimit"].is_number_integer()) {
      return std::nullopt;
    }
    std::vector<std::string> readonly_directories;
    if (json.contains("readonlyDirectoryIds")) {
      if (!json["readonlyDirectoryIds"].is_array()) {
        return std::nullopt;
      }
      for (const nlohmann::json& id : json["readonlyDirectoryIds"]) {
        if (!id.is_string()) {
          return std::nullopt;
        }
        readonly_directories.push_back(id.get<std::string>());
      }
    }
    Task task(
        json["id"].get<std::string>(),
        json["title"].get<std::string>(),
        json["description"].get<std::string>(),
        json["workingDirectory"].get<std::string>(),
        json["tokenLimit"].get<int>());
    task.readonly_directories_ = std::move(readonly_directories);
    return task;
  }

  Task() {}

  Task(const std::string& id, const std::string& title, const std::string& description,
      const std::string& working_directory, const int token_limit) :
      id_(id), title_(title), description_(description),
      working_directory_(working_directory), token_limit_(token_limit) {};

  Task(const std::string& id, const std::string& title, const std::string& description,
      const std::string& working_directory, const int token_limit,
      const std::vector<std::string>& readonly_directories) :
      id_(id), title_(title), description_(description),
      working_directory_(working_directory), token_limit_(token_limit),
      readonly_directories_(readonly_directories) {};

};

}  // namespace jiaolong