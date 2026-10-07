cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Determine the prebuilt libraries path. This mirrors the parent
# agent_and_server project so that the same prebuilt dependencies (nlohmann/json)
# can be reused. Must be an absolute path WITHOUT a trailing slash character
# pointing to a directory.
if(DEFINED ENV{JIAOLONG_PREBUILTS_PATH_ENV})
    set(JIAOLONG_PREBUILTS_PATH "$ENV{JIAOLONG_PREBUILTS_PATH_ENV}")
else()
    set(JIAOLONG_PREBUILTS_PATH "/jiaolong_external/prebuilt")
endif()

project(reverse_proxy LANGUAGES CXX)

# Tell CMake to search the local prebuilt folder first.
list(APPEND CMAKE_PREFIX_PATH "${JIAOLONG_PREBUILTS_PATH}/nlohmann_json")
find_package(nlohmann_json REQUIRED CONFIG)

add_executable(reverse_proxy
    main.cc
    reverse_proxy.cc
    reverse_proxy.h
    settings.cc
    settings.h
)

target_include_directories(reverse_proxy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${JIAOLONG_PREBUILTS_PATH}/nlohmann_json/include"
)

target_link_libraries(reverse_proxy PRIVATE nlohmann_json::nlohmann_json)