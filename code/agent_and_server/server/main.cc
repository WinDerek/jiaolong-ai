#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "server/jiaolong_server.h"

namespace {

constexpr int kDefaultPort = 8989;
constexpr char kPortFlag[] = "--port";

// Parses the command line for "--port <value>" (also accepts
// "--port=<value>"). Returns the default port when the flag is absent, or -1
// when the flag is missing its value or the value is not a valid port.
int ParsePort(int argc, char* argv[]) {
  int port = kDefaultPort;
  const std::string port_flag = kPortFlag;
  const std::string port_flag_with_equals = port_flag + "=";

  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    std::string value;
    if (arg == port_flag) {
      if (i + 1 >= argc) {
        std::cerr << "Error: " << port_flag << " requires a value." << std::endl;
        return -1;
      }
      value = argv[++i];
    } else if (arg.compare(0, port_flag_with_equals.size(),
                           port_flag_with_equals) == 0) {
      value = arg.substr(port_flag_with_equals.size());
    }

    if (!value.empty()) {
      port = std::atoi(value.c_str());
    }
  }

  if (port < 1 || port > 65535) {
    std::cerr << "Error: invalid port " << port << "." << std::endl;
    return -1;
  }
  return port;
}

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

}  // namespace

int main(int argc, char* argv[]) {
  const int port = ParsePort(argc, argv);
  if (port < 0) {
    return 1;
  }

  std::string jiaolong_cli_path = "";
  std::string jj_executable_path = "";
  std::string github_token = "";
  std::string gh_executable_path = "";
  std::string socks_proxy = "";
  std::string mac_system_health_data_server_domain = "";
  std::string username = "";
  std::string password = "";
  const std::optional<nlohmann::json> settings_opt = LoadSettings();
  if (!settings_opt.has_value()) {
    return 1;
  }
  const nlohmann::json& settings_json = *settings_opt;

  // The server expects the following settings fields to exist; verify them
  // manually instead of relying on exceptions from JSON lookups.
  constexpr char kRequiredFields[][32] = {
      "jiaolongCliPath", "jjExecutablePath", "githubToken",
      "ghExecutablePath", "username", "password"};
  for (const char* field : kRequiredFields) {
    if (!settings_json.contains(field) || !settings_json[field].is_string()) {
      std::cerr << "Error: settings file is missing required string field: "
                << field << std::endl;
      return 1;
    }
  }
  jiaolong_cli_path = settings_json["jiaolongCliPath"];
  jj_executable_path = settings_json["jjExecutablePath"];
  // The Jiaolong Server expects the full path to the GitHub CLI (gh)
  // executable to exist in the settings file under `ghExecutablePath`.
  github_token = settings_json["githubToken"];
  gh_executable_path = settings_json["ghExecutablePath"];
  // The SOCKS proxy address used by the VCS tools for remote operations
  // (`jj git fetch`, `jj git push`). It is configured in the settings file
  // under `socksProxy`; when absent, a warning is printed to stdout and the
  // default value `127.0.0.1:7891` is used.
  socks_proxy = settings_json.value("socksProxy", "");
  if (socks_proxy.empty()) {
    std::cout << "Warning: socksProxy is not set, using default value "
                 "127.0.0.1:7891" << std::endl;
    socks_proxy = "127.0.0.1:7891";
  }
  // The domain of the mac system health data server that the server's system
  // health perception loop requests the data from. It is configured in the
  // settings file under `macSystemHealthDataServerDomain`; when absent, a
  // warning is printed to stdout and the default value `127.0.0.1` is used.
  // The Jiaolong Server now runs on the host itself rather than in a
  // container, so the mac system health data server is reached locally by
  // default.
  mac_system_health_data_server_domain =
      settings_json.value("macSystemHealthDataServerDomain", "");
  if (mac_system_health_data_server_domain.empty()) {
    std::cout << "Warning: macSystemHealthDataServerDomain is not set, using "
                 "default value 127.0.0.1" << std::endl;
    mac_system_health_data_server_domain = "127.0.0.1";
  }
  // The client credentials (username/password) used to authenticate every
  // REST API call are configured in the settings file and are read by the
  // server; clients can never modify them.
  username = settings_json["username"];
  password = settings_json["password"];

  jiaolong::server::JiaolongServer server(jiaolong_cli_path, jj_executable_path,
      github_token, gh_executable_path, socks_proxy,
      mac_system_health_data_server_domain, username, password);
  server.RegisterRoutes();

  std::cout << "Jiaolong Server is listening on port " << port << "."
            << std::endl;
  if (!server.Start(port)) {
    std::cerr << "Error: failed to start Jiaolong Server on port " << port
              << "." << std::endl;
    return 1;
  }
  return 0;
}