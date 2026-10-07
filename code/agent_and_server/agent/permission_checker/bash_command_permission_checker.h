#pragma once

#include <string>
#include <filesystem>
#include <vector>

#include "agent/permission_checker/permission_check_result.h"

namespace jiaolong {

class BashCommandPermissionChecker {

 public:

  BashCommandPermissionChecker(const std::vector<std::filesystem::path>& allowed_paths);

  PermissionCheckResult CheckCommand(const std::string& command,
      const std::string& working_directory);

 private:

  const std::vector<std::filesystem::path>& allowed_paths_;

};

}  // namespace jiaolong
