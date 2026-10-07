cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(mac_system_health_data_server LANGUAGES C CXX)

set(HTTPLIB_ROOT "~/.jiaolong/jiaolong_external/prebuilt/httplib")

# macOS frameworks required to talk to the Apple System Management Controller.
find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)

# Find the threading library (pthread on macOS).
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Library that reads system health data. DataCollector is the base class;
# MacSystemHealthDataCollector reads through the Apple SMC and
# PowermetricsDataCollector reads through the powermetrics command line tool.
add_library(mac_system_health_data_collector_lib
    health/data_collector.h
    health/data_collector.cc
    health/mac_system_health_data_collector.h
    health/mac_system_health_data_collector.cc
    health/powermetrics_data_collector.h
    health/powermetrics_data_collector.cc
)
target_include_directories(mac_system_health_data_collector_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(mac_system_health_data_collector_lib
    PUBLIC ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK}
)

# HTTP server executable exposing the latest system health data frame via a
# REST API, built with the prebuilt header-only httplib.
add_executable(mac_system_health_data_server
    server/main.cc
    server/mac_system_health_data_server.h
    server/mac_system_health_data_server.cc
)
target_include_directories(mac_system_health_data_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HTTPLIB_ROOT}/include
    ${HTTPLIB_ROOT}
)
target_link_libraries(mac_system_health_data_server PRIVATE
    mac_system_health_data_collector_lib
    Threads::Threads
)
