#pragma once

#include <string>
#include <utility>

namespace jiaolong {

// Runs `command` through the system shell and captures its combined stdout and
// stderr output. Returns { exit_code, output }; a -1 exit code means the
// command could not be started or its exit status could not be determined.
std::pair<int, std::string> RunCommandAndCaptureOutput(
    const std::string& command);

}  // namespace jiaolong
