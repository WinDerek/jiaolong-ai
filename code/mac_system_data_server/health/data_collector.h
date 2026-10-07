#pragma once

#include <optional>
#include <string>

namespace mac_system_health {

// Base class for Mac system health data collectors. Concrete subclasses
// implement different methods of reading the CPU temperature (e.g. through the
// Apple SMC or via the powermetrics command line tool).
class DataCollector {
 public:
  virtual ~DataCollector() = default;

  DataCollector() = default;
  DataCollector(const DataCollector&) = delete;
  DataCollector& operator=(const DataCollector&) = delete;

  // Reads the current CPU temperature from the system in degrees Celsius.
  // Returns std::nullopt when the value cannot be read.
  virtual std::optional<double> ReadCpuTemperatureCelsius() = 0;

  // Collects the latest system health data frame and returns it as a JSON
  // string, e.g. {"cpuTemperatureCelsius":48.50}. Returns std::nullopt when no
  // CPU temperature can be read.
  std::optional<std::string> CollectLatestFrameJson();
};

}  // namespace mac_system_health