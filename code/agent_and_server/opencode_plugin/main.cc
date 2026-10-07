#include <iostream>
#include <string>
#include <fstream>
#include <format>
#include <chrono>

#include <nlohmann/json.hpp>

#include "agent/permission_checker/claude_permission_checker.h"

namespace jiaolong {

std::string GetCurrentTimestamp() {
  auto now = std::chrono::system_clock::now();
  return std::format("{:%Y%m%d_%H%M%S}", now);
}

// Append a log line into the log file
void AppendLog(const nlohmann::json& parameters_json,
    const PermissionCheckResult& result) {
  std::ofstream log_file("/home/user/opencode_plugin_log.txt", std::ios_base::app);
  if (log_file.is_open()) {
    log_file << GetCurrentTimestamp() <<
        " allowed: " << (result.allowed ? "true" : "false") <<
        ", reason: \"" << result.reason << "\", parameters: " <<
        parameters_json << std::endl;
    log_file.close();
  }
}

}  // namespace jiaolong

int main(int argc, char* argv[]) {
  // Read stdin and parse as JSON
  std::string input((std::istreambuf_iterator<char>(std::cin)),
      std::istreambuf_iterator<char>());
  nlohmann::json input_json = nlohmann::json::parse(input);

  // Create the permission checker
  jiaolong::ClaudePermissionChecker permission_checker;

  // TODO: Run real checking logic.
  // jiaolong::PermissionCheckResult result = permission_checker.CheckPermission(
  //     input_json);
  jiaolong::PermissionCheckResult result;
  const std::string output_json_str = std::format(R"(
    {{
      "decision": "{}",
      "reason": "{}"
    }}
  )", result.allowed ? "allow" : "deny", result.reason);
  std::cout << output_json_str << std::endl;

  // Write log
  // TODO: Test only. Remove me.
  jiaolong::AppendLog(input_json, result);

  return 0;
}
