#include "server/system_health_data_manager.h"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace jiaolong {
namespace server {

namespace {

// Port and endpoint of the mac system health data server running on the macOS
// host. The Jiaolong Server now runs on the host itself rather than in a
// container, so the domain of the mac system health data server is provided
// by the caller (the server main.cc reads it from the settings file and
// applies the default).
constexpr int kMacSystemHealthServerPort = 60667;
constexpr char kMacSystemHealthServerPath[] = "/api/system-health/latest";

// Request timeouts in milliseconds. Kept short so a failing host does not
// unacceptably delay the one-second perception loop.
constexpr int kHttpTimeoutMs = 500;

// Returns the current Unix time in milliseconds.
int64_t NowMillis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

std::optional<double>
SystemHealthDataManager::DefaultHostCpuTemperatureReader(
    const std::string& domain) {
  httplib::Client client(domain, kMacSystemHealthServerPort);
  client.set_connection_timeout(kHttpTimeoutMs / 1000,
                                (kHttpTimeoutMs % 1000) * 1000);
  client.set_read_timeout(kHttpTimeoutMs / 1000,
                          (kHttpTimeoutMs % 1000) * 1000);

  const httplib::Result response = client.Get(kMacSystemHealthServerPath);
  if (!response || response->status != 200) {
    // The mac system health data server is unreachable or returned an error;
    // treat it as no data.
    return std::nullopt;
  }

  const nlohmann::json body =
      nlohmann::json::parse(response->body, nullptr, false);
  if (body.is_discarded() || !body.is_object() ||
      !body.contains("cpuTemperatureCelsius") ||
      !body["cpuTemperatureCelsius"].is_number()) {
    return std::nullopt;
  }
  return body["cpuTemperatureCelsius"].get<double>();
}

SystemHealthDataManager::SystemHealthDataManager(
    const std::string& mac_system_health_data_server_domain,
    std::chrono::milliseconds loop_interval,
    CpuTemperatureReader cpu_temperature_reader) :
    loop_interval_(loop_interval),
    cpu_temperature_reader_(
        cpu_temperature_reader
            ? cpu_temperature_reader
            : [mac_system_health_data_server_domain]() {
                return DefaultHostCpuTemperatureReader(
                    mac_system_health_data_server_domain);
              }) {}

SystemHealthDataManager::~SystemHealthDataManager() {
  Stop();
}

void SystemHealthDataManager::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    // The perception loop is already running.
    return;
  }
  loop_thread_ = std::thread(&SystemHealthDataManager::PerceptionLoop, this);
}

void SystemHealthDataManager::Stop() {
  running_.store(false);
  if (loop_thread_.joinable()) {
    loop_thread_.join();
  }
}

bool SystemHealthDataManager::IsRunning() const {
  return running_.load();
}

void SystemHealthDataManager::RunOnce() {
  const std::optional<double> cpu_temperature = cpu_temperature_reader_();
  if (!cpu_temperature.has_value()) {
    // The system health data server is unavailable (the request failed); treat
    // this run as no data and do not record a frame.
    return;
  }

  SystemHealthDataFrame frame;
  frame.timestamp_ms = NowMillis();
  frame.cpu_temperature_celsius = *cpu_temperature;

  std::lock_guard<std::mutex> lock(mutex_);
  frames_.push_back(frame);
  while (frames_.size() > kMaxFrames) {
    frames_.pop_front();
  }
}

void SystemHealthDataManager::PerceptionLoop() {
  while (running_.load()) {
    RunOnce();
    // Sleep in small steps so Stop() remains responsive even with a long loop
    // interval.
    constexpr std::chrono::milliseconds kSleepStep(10);
    std::chrono::milliseconds remaining = loop_interval_;
    while (remaining > std::chrono::milliseconds::zero() &&
           running_.load()) {
      const std::chrono::milliseconds sleep_for =
          remaining < kSleepStep ? remaining : kSleepStep;
      std::this_thread::sleep_for(sleep_for);
      remaining -= sleep_for;
    }
  }
}

nlohmann::json SystemHealthDataManager::GetLastFrames(int count) const {
  const std::vector<SystemHealthDataFrame> frames = GetLastFramesVector(count);
  nlohmann::json result = nlohmann::json::array();
  for (const SystemHealthDataFrame& frame : frames) {
    result.push_back(nlohmann::json{
        {"timestamp", frame.timestamp_ms},
        {"cpuTemperatureCelsius", frame.cpu_temperature_celsius},
    });
  }
  return result;
}

std::vector<SystemHealthDataFrame> SystemHealthDataManager::GetLastFramesVector(
    int count) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<SystemHealthDataFrame> result;
  if (count <= 0 || frames_.empty()) {
    return result;
  }
  const size_t begin = frames_.size() > static_cast<size_t>(count)
      ? frames_.size() - static_cast<size_t>(count)
      : 0;
  result.assign(frames_.begin() + static_cast<std::ptrdiff_t>(begin),
                frames_.end());
  return result;
}

}  // namespace server
}  // namespace jiaolong