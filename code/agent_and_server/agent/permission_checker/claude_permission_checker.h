#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "agent/permission_checker/bash_command_permission_checker.h"
#include "agent/permission_checker/permission_check_result.h"

namespace jiaolong {

class ClaudePermissionChecker {

 public:

  ClaudePermissionChecker();

  PermissionCheckResult CheckPermission(const nlohmann::json& parameters_json);

 private:

  BashCommandPermissionChecker bash_command_permission_checker_;

  std::vector<std::filesystem::path> allowed_working_directories_;
  std::vector<std::filesystem::path> allowed_target_paths_;
  std::unordered_set<std::string> allowed_tools_;

};

}  // namespace jiaolong
