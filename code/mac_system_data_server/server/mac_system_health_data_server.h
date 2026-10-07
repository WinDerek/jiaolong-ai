#pragma once

#include <httplib.h>

#include "health/powermetrics_data_collector.h"

namespace mac_system_health {

// HTTP server that exposes the latest Mac system health data frame through a
// small REST API. Data frames are produced by PowermetricsDataCollector, which
// reads the values by running the `powermetrics` command line tool.
class MacSystemHealthDataServer {
 public:
  MacSystemHealthDataServer() = default;

  // Registers request logging and the REST endpoints.
  void RegisterRoutes();

  // Starts the HTTP server on the given port and blocks until it stops.
  // Returns false when the server could not be started.
  bool Start(int port);

 private:
  httplib::Server http_server_;
  PowermetricsDataCollector collector_;
};

}  // namespace mac_system_health