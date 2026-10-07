#pragma once

#include <string>
#include <vector>

namespace jiaolong {

struct PermissionCheckResult {
  bool allowed = false;
  std::string reason;
  // The resolved real host path(s) of the tool call's path argument(s), in
  // argument order. Empty when the call was denied or when the tool has no
  // path argument.
  std::vector<std::string> real_paths;
};

}  // namespace jiaolong