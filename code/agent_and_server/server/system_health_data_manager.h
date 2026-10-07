#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace jiaolong {
namespace server {

// A single frame of system health data gathered during one perception loop
// run.
struct SystemHealthDataFrame {
  // Unix timestamp in milliseconds when the frame was gathered.
  int64_t timestamp_ms = 0;
  // CPU temperature in degrees Celsius.
  double cpu_temperature_celsius = 0.0;
};

// Maintains a perception loop that gathers system health data (currently just
// the CPU temperature) from the mac system health data server on its own
// thread.
//
// The loop starts when Start() is called (the Jiaolong Server calls it on
// boot) and gathers one data frame per loop interval until Stop() is called.
// The data source is the mac system health data server running on the macOS
// host, which this manager requests through its HTTP API at the domain given
// to the constructor. When a request fails, the manager treats it as no data
// and does not record a frame. Tests can inject a custom reader through the
// constructor.
class SystemHealthDataManager {
 public:
  // Signature of the (injectable) function that gathers one CPU temperature
  // reading from the mac system health data server in degrees Celsius.
  // Returns std::nullopt when the reading could not be gathered (for example
  // when the HTTP request fails), which the perception loop treats as no
  // data.
  using CpuTemperatureReader = std::function<std::optional<double>()>;

  // The default CPU temperature reader for the mac system health data server
  // running at `domain`. Requests the latest CPU temperature from its HTTP
  // API on port 60667 (/api/system-health/latest); returns std::nullopt when
  // the request fails.
  static std::optional<double> DefaultHostCpuTemperatureReader(
      const std::string& domain);

  // Constructs the manager. `mac_system_health_data_server_domain` is the
  // mandatory domain (host) of the mac system health data server to gather
  // the data from; `loop_interval` is the delay between two perception loop
  // runs (one second by default); `cpu_temperature_reader` is the function
  // used to gather the CPU temperature (defaults to requesting the mac system
  // health data server at `mac_system_health_data_server_domain`).
  explicit SystemHealthDataManager(
      const std::string& mac_system_health_data_server_domain,
      std::chrono::milliseconds loop_interval = std::chrono::seconds(1),
      CpuTemperatureReader cpu_temperature_reader = {});

  // Non-copyable because the perception loop thread cannot be copied.
  SystemHealthDataManager(const SystemHealthDataManager&) = delete;
  SystemHealthDataManager& operator=(const SystemHealthDataManager&) = delete;

  // Stops the perception loop thread (if running) and joins it.
  ~SystemHealthDataManager();

  // Starts the perception loop thread. No-op when the loop is already
  // running.
  void Start();

  // Stops the perception loop thread and joins it. Safe to call even when the
  // loop has never been started or has already been stopped.
  void Stop();

  // Returns true when the perception loop thread is running.
  bool IsRunning() const;

  // Returns the last `count` frames as a JSON array (oldest first). When
  // fewer than `count` frames have been gathered, all stored frames are
  // returned; the array is empty when no frame has been gathered yet. Each
  // frame has the shape:
  //   {"timestamp": <int64 milliseconds>, "cpuTemperatureCelsius": <double>}
  nlohmann::json GetLastFrames(int count) const;

  // Returns the last `count` frames as a vector (oldest first).
  std::vector<SystemHealthDataFrame> GetLastFramesVector(int count) const;

  // Maximum number of frames kept in memory; older frames are discarded.
  static constexpr size_t kMaxFrames = 1000;

 private:
  // Runs one perception loop iteration: gathers a frame and stores it.
  void RunOnce();

  // Body of the perception loop thread.
  void PerceptionLoop();

  const std::chrono::milliseconds loop_interval_;
  const CpuTemperatureReader cpu_temperature_reader_;

  mutable std::mutex mutex_;
  std::deque<SystemHealthDataFrame> frames_;
  std::atomic<bool> running_{false};
  std::thread loop_thread_;
};

}  // namespace server
}  // namespace jiaolong