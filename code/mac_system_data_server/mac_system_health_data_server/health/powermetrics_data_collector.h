#pragma once

#include <optional>
#include <string>

#include "health/data_collector.h"

namespace mac_system_health {

// Reads Mac system health data by running the `powermetrics` command line tool
// and parsing its stdout. This collector does not depend on the Apple SMC
// user-space interface.
class PowermetricsDataCollector : public DataCollector {
 public:
  PowermetricsDataCollector() = default;

  // Reads the current CPU temperature by running
  // `powermetrics --samplers smc -n 1 -i 1 | grep 'CPU die temperature'` and
  // parsing the reported value in degrees Celsius. Returns std::nullopt when
  // the command fails or the value cannot be parsed.
  std::optional<double> ReadCpuTemperatureCelsius() override;

 private:
  // Runs the powermetrics command and returns its stdout. Returns std::nullopt
  // when the command cannot be started or exits with a non-zero status.
  std::optional<std::string> RunPowermetricsCommand();

  // Parses a powermetrics stdout line such as
  // "CPU die temperature: 48.50 C" into degrees Celsius. Returns std::nullopt
  // when no temperature value can be found.
  std::optional<double> ParseCpuTemperatureCelsius(
      const std::string& output);
};

}  // namespace mac_system_health