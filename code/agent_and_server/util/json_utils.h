#pragma once

#include <cstdlib>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

namespace jiaolong {

// Reads `field` from the JSON object `json`. If `json` is not an object, or the
// field does not exist, the process fails fast: an error naming the missing
// field is printed to stderr and the program exits immediately with a
// non-zero exit code. This turns an otherwise silent crash (e.g. an
// nlohmann::json exception) into a debuggable error message.
inline const nlohmann::json& RequireJsonField(const nlohmann::json& json,
    const std::string& field) {
  if (!json.is_object() || !json.contains(field)) {
    std::cerr << "Error: JSON is missing required field \"" << field << "\"."
              << std::endl;
    std::exit(1);
  }
  return json.at(field);
}

// Same as RequireJsonField, but additionally requires the field to be a
// string.
inline std::string RequireStringJsonField(const nlohmann::json& json,
    const std::string& field) {
  const nlohmann::json& value = RequireJsonField(json, field);
  if (!value.is_string()) {
    std::cerr << "Error: JSON field \"" << field << "\" must be a string."
              << std::endl;
    std::exit(1);
  }
  return value.get<std::string>();
}

// Same as RequireJsonField, but additionally requires the field to be an
// integer.
inline int RequireIntJsonField(const nlohmann::json& json,
    const std::string& field) {
  const nlohmann::json& value = RequireJsonField(json, field);
  if (!value.is_number_integer()) {
    std::cerr << "Error: JSON field \"" << field << "\" must be an integer."
              << std::endl;
    std::exit(1);
  }
  return value.get<int>();
}

}  // namespace jiaolong
