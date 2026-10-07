#include "health/powermetrics_data_collector.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace mac_system_health {

namespace {

// The exact powermetrics command whose stdout this collector parses.
constexpr char kPowermetricsCommand[] =
    "powermetrics --samplers smc -n 1 -i 1 | grep 'CPU die temperature'";

// Marker that precedes the CPU die temperature value on the matched line.
const std::string kCpuTemperatureMarker = "CPU die temperature:";

}  // namespace

std::optional<std::string>
PowermetricsDataCollector::RunPowermetricsCommand() {
  FILE* pipe = popen(kPowermetricsCommand, "r");
  if (pipe == nullptr) {
    return std::nullopt;
  }

  std::string output;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }

  const int status = pclose(pipe);
  if (status != 0) {
    return std::nullopt;
  }
  return output;
}

std::optional<double> PowermetricsDataCollector::ParseCpuTemperatureCelsius(
    const std::string& output) {
  const size_t marker_pos = output.find(kCpuTemperatureMarker);
  if (marker_pos == std::string::npos) {
    return std::nullopt;
  }

  size_t cursor = marker_pos + kCpuTemperatureMarker.size();
  while (cursor < output.size() &&
         std::isspace(static_cast<unsigned char>(output[cursor]))) {
    ++cursor;
  }

  std::string number;
  while (cursor < output.size()) {
    const char c = output[cursor];
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' ||
        c == '-' || c == '+') {
      number += c;
      ++cursor;
    } else {
      break;
    }
  }

  if (number.empty()) {
    return std::nullopt;
  }

  char* end = nullptr;
  const double value = std::strtod(number.c_str(), &end);
  if (end == number.c_str()) {
    return std::nullopt;
  }
  return value;
}

std::optional<double>
PowermetricsDataCollector::ReadCpuTemperatureCelsius() {
  const std::optional<std::string> output = RunPowermetricsCommand();
  if (!output.has_value()) {
    return std::nullopt;
  }
  return ParseCpuTemperatureCelsius(*output);
}

}  // namespace mac_system_health