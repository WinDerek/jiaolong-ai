#pragma once

#include <string>
#include <vector>

namespace reverse_proxy {

// Settings loaded from `~/.reverse_proxy/settings.json`.
struct Settings {
  // The shell commands that establish the SSH forwardings this program keeps
  // alive. Each command is executed through `/bin/sh -c`, so it may contain
  // shell constructs. The list must not be empty.
  std::vector<std::string> ssh_forwarding_commands;

  // How long to sleep between two liveness checks, in seconds.
  int check_interval_seconds = 5;
};

// Loads and validates the settings file at `path`. Returns true on success. On
// failure an explanatory error is printed to stderr and false is returned.
bool LoadSettings(const std::string& path, Settings* settings);

}  // namespace reverse_proxy