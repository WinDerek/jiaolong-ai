#include "settings.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace reverse_proxy {

namespace {

constexpr char kSshForwardingCommandsField[] = "sshForwardingCommands";
constexpr char kCheckIntervalSecondsField[] = "checkIntervalSeconds";

}  // namespace

bool LoadSettings(const std::string& path, Settings* settings) {
  std::ifstream settings_file(path);
  if (!settings_file.is_open()) {
    std::cerr << "Error: Failed to open settings file: " << path << std::endl;
    return false;
  }

  std::stringstream buffer;
  buffer << settings_file.rdbuf();

  // Parse without throwing so that a malformed file produces a clear error
  // instead of terminating the program with an exception.
  const nlohmann::json json =
      nlohmann::json::parse(buffer.str(), nullptr, false);
  if (json.is_discarded() || !json.is_object()) {
    std::cerr << "Error: Failed to parse settings file as a JSON object: "
              << path << std::endl;
    return false;
  }

  if (!json.contains(kSshForwardingCommandsField) ||
      !json[kSshForwardingCommandsField].is_array()) {
    std::cerr << "Error: Settings field \"" << kSshForwardingCommandsField
              << "\" must be an array of strings." << std::endl;
    return false;
  }

  std::vector<std::string> commands;
  for (const auto& item : json[kSshForwardingCommandsField]) {
    if (!item.is_string()) {
      std::cerr << "Error: Every entry of \"" << kSshForwardingCommandsField
                << "\" must be a string." << std::endl;
      return false;
    }
    const std::string command = item.get<std::string>();
    if (command.empty()) {
      std::cerr << "Error: \"" << kSshForwardingCommandsField
                << "\" must not contain empty strings." << std::endl;
      return false;
    }
    commands.push_back(command);
  }

  if (commands.empty()) {
    std::cerr << "Error: \"" << kSshForwardingCommandsField
              << "\" must contain at least one SSH forwarding command."
              << std::endl;
    return false;
  }
  settings->ssh_forwarding_commands = std::move(commands);

  if (json.contains(kCheckIntervalSecondsField)) {
    if (!json[kCheckIntervalSecondsField].is_number_integer() ||
        json[kCheckIntervalSecondsField].get<int>() <= 0) {
      std::cerr << "Error: \"" << kCheckIntervalSecondsField
                << "\" must be a positive integer." << std::endl;
      return false;
    }
    settings->check_interval_seconds =
        json[kCheckIntervalSecondsField].get<int>();
  }

  return true;
}

}  // namespace reverse_proxy