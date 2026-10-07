#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "agent/session_history_visualizer/session_history_visualizer.h"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: jiaolong_session_history_visualizer_cli "
        "<session_history_file_path>" << std::endl;
    return 1;
  }

  const std::string session_history_file_path = argv[1];
  std::ifstream session_history_file(session_history_file_path);
  if (!session_history_file.is_open()) {
    std::cerr << "Failed to open session history file: " <<
        session_history_file_path << std::endl;
    return 1;
  }

  std::stringstream session_history_buffer;
  session_history_buffer << session_history_file.rdbuf();
  nlohmann::json session_history_json =
      nlohmann::json::parse(session_history_buffer.str(), nullptr, false);
  if (session_history_json.is_discarded()) {
    std::cerr << "Failed to parse session history file: " <<
        session_history_file_path << std::endl;
    return 1;
  }

  // Ensure the input file location is always shown at the top of the generated
  // page, even if the session history JSON does not already carry it.
  if (!session_history_json.contains("fileLocation")) {
    session_history_json["fileLocation"] = session_history_file_path;
  }

  jiaolong::SessionHistoryVisualizer visualizer;
  const std::string html = visualizer.GenerateHtml(session_history_json);

  // Write the generated HTML file next to the input file with a `.html`
  // extension.
  const std::string output_file_path = session_history_file_path + ".html";
  std::ofstream output_file(output_file_path);
  if (!output_file.is_open()) {
    std::cerr << "Failed to open output file for writing: " <<
        output_file_path << std::endl;
    return 1;
  }
  output_file << html;
  output_file.close();

  std::cout << "Session history visualization written to: " <<
      output_file_path << std::endl;
  return 0;
}
