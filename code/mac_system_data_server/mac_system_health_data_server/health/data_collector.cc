#include "health/data_collector.h"

#include <iomanip>
#include <sstream>

namespace mac_system_health {

std::optional<std::string> DataCollector::CollectLatestFrameJson() {
  const std::optional<double> temperature = ReadCpuTemperatureCelsius();
  if (!temperature.has_value()) {
    return std::nullopt;
  }

  std::ostringstream oss;
  oss << "{\"cpuTemperatureCelsius\":" << std::fixed << std::setprecision(2)
      << *temperature << "}";
  return oss.str();
}

}  // namespace mac_system_health