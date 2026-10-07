#pragma once

#include <optional>
#include <string>

#include <IOKit/IOKitLib.h>

#include "health/data_collector.h"

namespace mac_system_health {

// Reads Mac system health data from the Apple System Management Controller
// (SMC) through the user-space IOKit SMC interface (the AppleSMC IOService).
class MacSystemHealthDataCollector : public DataCollector {
 public:
  // Opens the AppleSMC IOService. The collector is usable when IsOpen()
  // returns true; otherwise the read methods return std::nullopt.
  MacSystemHealthDataCollector();

  // Closes the AppleSMC IOService.
  ~MacSystemHealthDataCollector();

  MacSystemHealthDataCollector(const MacSystemHealthDataCollector&) = delete;
  MacSystemHealthDataCollector& operator=(const MacSystemHealthDataCollector&) =
      delete;

  // Returns true when the SMC connection is open.
  bool IsOpen() const { return connection_ != 0; }

  // Reads the current CPU temperature from the SMC in degrees Celsius.
  // Returns std::nullopt when the SMC is not available or the value cannot be
  // read.
  std::optional<double> ReadCpuTemperatureCelsius() override;

 private:
  // Opens the AppleSMC IOService; returns true on success.
  bool Open();

  // Closes the AppleSMC IOService.
  void Close();

  // Reads the raw payload bytes of a 4-character SMC key (e.g. "TC0P").
  // Returns true on success and fills `bytes` with up to 32 payload bytes.
  bool ReadKeyData(const std::string& key, unsigned char bytes[32]);

  // Decodes the raw payload of a temperature key into degrees Celsius.
  std::optional<double> DecodeTemperature(const unsigned char bytes[32]);

  io_connect_t connection_;

};

}  // namespace mac_system_health
