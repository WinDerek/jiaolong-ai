#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "agent/tool/tool.h"
#include "agent/tool/tool_use_status.h"

namespace jiaolong {

// A single whitelisted bash command read from the settings file. Each settings
// entry is a JSON object with exactly two keys: `bashCommand` and
// `workingDirectory`. A bash tool invocation is executed only when both the
// command text and the working directory match an allowed entry exactly.
struct AllowedBashCommand {
  std::string bash_command;
  std::string working_directory;
};

// Input and output for ToolBash.
class ToolBashInput : public ToolUseInput {
 public:
  const std::string bash_command_;
  const std::string working_directory_;
  ToolBashInput(const std::string& bash_command,
      const std::string& working_directory) :
      ToolUseInput(ToolType::kBash), bash_command_(bash_command),
      working_directory_(working_directory) {}
};
class ToolBashOutput : public ToolUseOutput {
 public:
  const std::string stdout_output_;
  ToolBashOutput(const std::string& stdout_output) :
      ToolUseOutput(ToolType::kBash), stdout_output_(stdout_output) {}
};

// Bash tool that runs a whitelisted bash command in a working directory. The
// whitelist is configured in the settings file under the `allowedBashCommands`
// field, an array of objects each containing `bashCommand` and
// `workingDirectory`. It accepts two input parameters (the bash command and
// the working directory) and returns the combined stdout and stderr of the
// command.
class ToolBash : public Tool {

 public:

  ToolBash(const std::vector<AllowedBashCommand>& allowed_bash_commands) :
      Tool(ToolType::kBash, PermissionType::kWrite),
      allowed_bash_commands_(allowed_bash_commands) {}

  // Runs the whitelisted bash command inside the working directory and returns
  // its combined stdout and stderr.
  std::pair<std::shared_ptr<ToolUseStatus>, ToolBashOutput> Execute(
      const ToolBashInput& input);

  const std::vector<AllowedBashCommand>& allowed_bash_commands() const {
    return allowed_bash_commands_;
  }

 private:

  const std::vector<AllowedBashCommand> allowed_bash_commands_;

};

}  // namespace jiaolong