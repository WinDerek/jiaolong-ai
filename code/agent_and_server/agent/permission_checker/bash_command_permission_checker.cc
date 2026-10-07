#include <string>
#include <filesystem>
#include <vector>
#include <unordered_set>
#include <sstream>

#include "agent/permission_checker/bash_command_permission_checker.h"
#include "agent/permission_checker/path_utils.h"

namespace jiaolong {

namespace {

std::vector<std::string> TokenizeBashCommand(const std::string& command) {
  std::stringstream ss(command);
  std::string token;
  std::vector<std::string> tokens;
  while (ss >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

}

BashCommandPermissionChecker::BashCommandPermissionChecker(
    const std::vector<std::filesystem::path>& allowed_paths) :
        allowed_paths_(allowed_paths) {}

PermissionCheckResult BashCommandPermissionChecker::CheckCommand(
    const std::string& command, const std::string& working_directory) {
  PermissionCheckResult result;

  // Tokenization
  const std::vector<std::string> tokens = TokenizeBashCommand(command);
  const std::string& executable = tokens[0];

  // ls, cat, touch
  // TODO: Support find, mkdir
  const std::unordered_set<std::string> executables_only_needs_path_check = {
    "ls", "cat", "touch"
  };
  if (executables_only_needs_path_check.contains(executable)) {
    const int num_token = tokens.size();
    for (int token_idx = 1; token_idx < num_token; token_idx++) {
      const std::string& token = tokens[token_idx];

      // Skip options
      if (token[0] == '-') {
        continue;
      }

      // Deal with parameters (which should be paths)
      const std::filesystem::path absolute_path =
          ToAbsolutePath(token, working_directory);
      if (!IsPathAllowed(absolute_path, allowed_paths_)) {
        result.allowed = false;
        result.reason = "Path not allowed, path: \"" +
            absolute_path.string() + "\"";
        return result;
      }
    }

    // All paths are allowed, allow
    result.allowed = true;
    return result;
  }

  result.allowed = false;
  result.reason = "Executable not allowed, executable: \"" + executable + "\"";

  return result;
}

}  // namespace jiaolong
