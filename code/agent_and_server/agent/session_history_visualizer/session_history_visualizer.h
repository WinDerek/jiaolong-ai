#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace jiaolong {

class SessionHistoryVisualizer {

 public:

  // Generates a single self-contained HTML document which visualizes the
  // given session history JSON data.
  std::string GenerateHtml(const nlohmann::json& session_history_json);

};

}  // namespace jiaolong
