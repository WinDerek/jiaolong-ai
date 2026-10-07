#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "reverse_proxy.h"
#include "settings.h"

namespace {

constexpr char kSettingsFlag[] = "--settings";

// Returns the default settings path `~/.reverse_proxy/settings.json`, logging
// an error to stderr and returning std::nullopt when HOME is not set.
std::optional<std::string> DefaultSettingsPath() {
  const char* home_directory = std::getenv("HOME");
  if (home_directory == nullptr || *home_directory == '\0') {
    std::cerr << "Error: HOME is not set; cannot locate "
                 "~/.reverse_proxy/settings.json." << std::endl;
    return std::nullopt;
  }
  return (std::filesystem::path(home_directory) / ".reverse_proxy" /
          "settings.json")
      .string();
}

// Parses "--settings <value>" (also accepts "--settings=<value>"). Returns the
// default settings path when the flag is absent, or std::nullopt (after logging
// an error to stderr) when the flag is malformed or an unknown argument is
// given.
std::optional<std::string> ParseSettingsPath(int argc, char* argv[]) {
  const std::string flag = kSettingsFlag;
  const std::string flag_with_equals = flag + "=";

  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == flag) {
      if (i + 1 >= argc) {
        std::cerr << "Error: " << flag << " requires a value." << std::endl;
        return std::nullopt;
      }
      return std::string(argv[++i]);
    }
    if (arg.compare(0, flag_with_equals.size(), flag_with_equals) == 0) {
      return arg.substr(flag_with_equals.size());
    }
    std::cerr << "Error: unknown argument \"" << arg << "\"." << std::endl;
    return std::nullopt;
  }

  return DefaultSettingsPath();
}

}  // namespace

int main(int argc, char* argv[]) {
  const std::optional<std::string> settings_path =
      ParseSettingsPath(argc, argv);
  if (!settings_path.has_value()) {
    return 1;
  }

  reverse_proxy::Settings settings;
  if (!reverse_proxy::LoadSettings(*settings_path, &settings)) {
    return 1;
  }

  std::cout << "[reverse_proxy] Loaded "
            << settings.ssh_forwarding_commands.size()
            << " SSH forwarding command(s) from " << *settings_path << "."
            << std::endl;

  reverse_proxy::RunGuardLoop(settings.ssh_forwarding_commands,
                              settings.check_interval_seconds);
  return 0;
}