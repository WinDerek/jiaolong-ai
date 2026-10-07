#include "server/mac_system_health_data_server.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include <httplib.h>

#include "health/data_collector.h"

namespace mac_system_health {

namespace {

// Returns the current local time formatted as "YYYY-MM-DD HH:MM:SS".
std::string CurrentTimeString() {
  const std::chrono::system_clock::time_point now =
      std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm local_tm{};
  localtime_r(&now_time, &local_tm);
  std::ostringstream oss;
  oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

// Builds an error response body: {"error": "<message>"}.
std::string ErrorBody(const std::string& message) {
  std::ostringstream oss;
  oss << "{\"error\":\"";
  for (const char c : message) {
    if (c == '"' || c == '\\') {
      oss << '\\';
    }
    oss << c;
  }
  oss << "\"}";
  return oss.str();
}

// Writes the latest system health data frame, or a 503 error when the frame
// cannot be collected.
void WriteLatestFrame(DataCollector& collector,
                      httplib::Response& res) {
  const std::optional<std::string> frame = collector.CollectLatestFrameJson();
  if (!frame.has_value()) {
    res.status = 503;
    res.set_content(
        ErrorBody("failed to read system health data"),
        "application/json");
    return;
  }
  res.set_content(*frame, "application/json");
}

}  // namespace

void MacSystemHealthDataServer::RegisterRoutes() {
  http_server_.set_logger(
      [](const httplib::Request& req, const httplib::Response& res) {
        std::cout << "[" << CurrentTimeString() << "] " << req.method << " "
                  << req.target << " from " << req.remote_addr << " -> "
                  << res.status << std::endl;
      });

  // GET /api/system-health/latest
  // Returns the latest system health data frame, e.g.
  // {"cpuTemperatureCelsius":62.50}.
  http_server_.Get("/api/system-health/latest",
      [this](const httplib::Request&, httplib::Response& res) {
        WriteLatestFrame(collector_, res);
      });

  // GET /api/system-health
  // Alias for the latest system health data frame endpoint.
  http_server_.Get("/api/system-health",
      [this](const httplib::Request&, httplib::Response& res) {
        WriteLatestFrame(collector_, res);
      });

  // GET /
  // Service metadata.
  http_server_.Get("/", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(
        "{\"service\":\"mac-system-health-data-server\",\"status\":\"ok\"}",
        "application/json");
  });
}

bool MacSystemHealthDataServer::Start(int port) {
  // Bind to all interfaces so thin clients can reach the server remotely.
  return http_server_.listen("0.0.0.0", port);
}

}  // namespace mac_system_health