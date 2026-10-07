#include <cstdlib>
#include <iostream>
#include <string>

#include "server/mac_system_health_data_server.h"

namespace {

constexpr int kDefaultPort = 8988;
constexpr char kPortFlag[] = "--port";

// Parses the command line for "--port <value>" (also accepts
// "--port=<value>"). Returns the default port when the flag is absent, or -1
// when the flag is missing its value or the value is not a valid port.
int ParsePort(int argc, char* argv[]) {
  int port = kDefaultPort;
  const std::string port_flag = kPortFlag;
  const std::string port_flag_with_equals = port_flag + "=";

  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    std::string value;
    if (arg == port_flag) {
      if (i + 1 >= argc) {
        std::cerr << "Error: " << port_flag << " requires a value."
                  << std::endl;
        return -1;
      }
      value = argv[++i];
    } else if (arg.compare(0, port_flag_with_equals.size(),
                           port_flag_with_equals) == 0) {
      value = arg.substr(port_flag_with_equals.size());
    }

    if (!value.empty()) {
      port = std::atoi(value.c_str());
    }
  }

  if (port < 1 || port > 65535) {
    std::cerr << "Error: invalid port " << port << "." << std::endl;
    return -1;
  }
  return port;
}

}  // namespace

int main(int argc, char* argv[]) {
  const int port = ParsePort(argc, argv);
  if (port < 0) {
    return 1;
  }

  mac_system_health::MacSystemHealthDataServer server;
  server.RegisterRoutes();

  std::cout << "Mac System Health Data Server is listening on port " << port
            << "." << std::endl;
  if (!server.Start(port)) {
    std::cerr << "Error: failed to start Mac System Health Data Server on port "
              << port << "." << std::endl;
    return 1;
  }
  return 0;
}