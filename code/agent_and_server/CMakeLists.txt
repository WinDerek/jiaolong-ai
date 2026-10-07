cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Determine the prebuilt libraries path
# Must be an absolute path WITHOUT a trailing slash character pointing to a directory.
if(DEFINED ENV{JIAOLONG_PREBUILTS_PATH_ENV})
    set(JIAOLONG_PREBUILTS_PATH "$ENV{JIAOLONG_PREBUILTS_PATH_ENV}")
else()
    set(JIAOLONG_PREBUILTS_PATH "/jiaolong_external/prebuilt")
endif()

# Disable RTTI for better performance
string(APPEND CMAKE_CXX_FLAGS " -fno-rtti")

project(jiaolong_agent LANGUAGES C CXX)

add_library(jiaolong_agent_lib
    agent/jiaolong_agent.h
    agent/jiaolong_agent.cc
    agent/agent_turn.h
    agent/project.h
    agent/task.h
    agent/tool/tool.h
    agent/tool/tool_use_status.h
    agent/tool/tool_read.h
    agent/tool/tool_read.cc
    agent/tool/tool_edit.h
    agent/tool/tool_edit.cc
    agent/tool/tool_delete.h
    agent/tool/tool_delete.cc
    agent/tool/tool_move.h
    agent/tool/tool_move.cc
    agent/tool/tool_copy.h
    agent/tool/tool_copy.cc
    agent/tool/tool_find.h
    agent/tool/tool_find.cc
    agent/tool/tool_list_directory.h
    agent/tool/tool_list_directory.cc
    agent/tool/tool_vcs.h
    agent/tool/tool_vcs.cc
    agent/tool/tool_bash.h
    agent/tool/tool_bash.cc
)

add_library(jiaolong_util_lib
    util/uuid_generator.cc
    util/uuid_generator.h
    util/command_runner.cc
    util/command_runner.h
)

add_library(sqlite_lib
    "${JIAOLONG_PREBUILTS_PATH}/sqlite/sqlite3.h"
    "${JIAOLONG_PREBUILTS_PATH}/sqlite/sqlite3.c"
)

add_library(jiaolong_permission_checker_lib
    agent/permission_checker/permission_checker.cc
    agent/permission_checker/permission_checker.h
    agent/permission_checker/mount_table.cc
    agent/permission_checker/mount_table.h
    agent/permission_checker/claude_permission_checker.cc
    agent/permission_checker/claude_permission_checker.h
    agent/permission_checker/bash_command_permission_checker.cc
    agent/permission_checker/bash_command_permission_checker.h
    agent/permission_checker/path_utils.cc
    agent/permission_checker/path_utils.h
)

add_library(jiaolong_session_history_visualizer_lib
    agent/session_history_visualizer/session_history_visualizer.h
    agent/session_history_visualizer/session_history_visualizer.cc
)

# Tell CMake to search the local prebuilt folder first
list(APPEND CMAKE_PREFIX_PATH "${JIAOLONG_PREBUILTS_PATH}/cpr")
list(APPEND CMAKE_PREFIX_PATH "${JIAOLONG_PREBUILTS_PATH}/nlohmann_json")
list(APPEND CMAKE_PREFIX_PATH "${JIAOLONG_PREBUILTS_PATH}/stduuid")
list(APPEND CMAKE_PREFIX_PATH "${JIAOLONG_PREBUILTS_PATH}/ftxui")

# Locate the local configuration files
find_package(cpr REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(stduuid REQUIRED CONFIG)
find_package(ftxui REQUIRED CONFIG)

# Find the threading library (handles pthread on Linux/macOS automatically)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Executables
add_executable(jiaolong_cli
    agent/main.cc
)
add_executable(jiaolong_session_history_visualizer_cli
    agent/session_history_visualizer_main.cc
)
add_executable(claude_pre_tool_use_hook_main
    claude_pre_tool_use_hook/main.cc
)
add_executable(opencode_plugin_main
    opencode_plugin/main.cc
)
add_executable(jiaolong_server
    server/main.cc
    server/jiaolong_server.cc
    server/jiaolong_server.h
    server/client_credential_manager.cc
    server/client_credential_manager.h
    server/settings_service.cc
    server/settings_service.h
    server/system_health_data_manager.cc
    server/system_health_data_manager.h
    server/task_database.cc
    server/task_database.h
    server/task_service.cc
    server/task_service.h
    server/project_database.cc
    server/project_database.h
    server/project_service.cc
    server/project_service.h
)

target_link_libraries(jiaolong_agent_lib PUBLIC jiaolong_permission_checker_lib jiaolong_util_lib)
target_include_directories(jiaolong_agent_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${JIAOLONG_PREBUILTS_PATH}/cpr/include"
    "${JIAOLONG_PREBUILTS_PATH}/nlohmann_json/include"
    "${JIAOLONG_PREBUILTS_PATH}/ftxui/include"
)
target_include_directories(jiaolong_permission_checker_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${JIAOLONG_PREBUILTS_PATH}/nlohmann_json/include"
)

target_include_directories(jiaolong_session_history_visualizer_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${JIAOLONG_PREBUILTS_PATH}/nlohmann_json/include"
)
target_link_libraries(jiaolong_session_history_visualizer_lib PUBLIC nlohmann_json::nlohmann_json)

target_include_directories(jiaolong_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${JIAOLONG_PREBUILTS_PATH}/sqlite"
)
target_link_libraries(jiaolong_cli PRIVATE jiaolong_agent_lib jiaolong_util_lib cpr::cpr nlohmann_json::nlohmann_json ftxui::ftxui Threads::Threads sqlite_lib)

target_include_directories(jiaolong_util_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${JIAOLONG_PREBUILTS_PATH}/stduuid/include"
)

target_include_directories(jiaolong_session_history_visualizer_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(jiaolong_session_history_visualizer_cli PRIVATE jiaolong_session_history_visualizer_lib)

target_link_libraries(opencode_plugin_main PRIVATE jiaolong_permission_checker_lib nlohmann_json::nlohmann_json)
target_include_directories(opencode_plugin_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${JIAOLONG_PREBUILTS_PATH}/nlohmann_json/include")

target_link_libraries(claude_pre_tool_use_hook_main PRIVATE jiaolong_permission_checker_lib nlohmann_json::nlohmann_json)
target_include_directories(claude_pre_tool_use_hook_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${JIAOLONG_PREBUILTS_PATH}/nlohmann_json/include")

target_include_directories(jiaolong_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${JIAOLONG_PREBUILTS_PATH}/nlohmann_json/include"
    "${JIAOLONG_PREBUILTS_PATH}/httplib/include"
    "${JIAOLONG_PREBUILTS_PATH}/sqlite"
)
target_link_libraries(jiaolong_server PRIVATE jiaolong_agent_lib jiaolong_util_lib sqlite_lib Threads::Threads)

# Unit tests
enable_testing()
find_package(GTest REQUIRED)

add_executable(jiaolong_agent_test
    agent/jiaolong_agent_test.cc
)
target_include_directories(jiaolong_agent_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${JIAOLONG_PREBUILTS_PATH}/httplib/include"
)
target_link_libraries(jiaolong_agent_test PRIVATE
    jiaolong_agent_lib
    jiaolong_session_history_visualizer_lib
    cpr::cpr
    ftxui::ftxui
    Threads::Threads
    GTest::GTest
    GTest::Main
)

add_executable(jiaolong_tool_find_test
    agent/tool/tool_find_test.cc
)
target_include_directories(jiaolong_tool_find_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(jiaolong_tool_find_test PRIVATE
    jiaolong_agent_lib
    cpr::cpr
    GTest::GTest
    GTest::Main
)

add_executable(jiaolong_tool_read_test
    agent/tool/tool_read_test.cc
)
target_include_directories(jiaolong_tool_read_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(jiaolong_tool_read_test PRIVATE
    jiaolong_agent_lib
    cpr::cpr
    GTest::GTest
    GTest::Main
)

add_executable(jiaolong_tool_list_directory_test
    agent/tool/tool_list_directory_test.cc
)
target_include_directories(jiaolong_tool_list_directory_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(jiaolong_tool_list_directory_test PRIVATE
    jiaolong_agent_lib
    cpr::cpr
    GTest::GTest
    GTest::Main
)

add_executable(jiaolong_tool_delete_test
    agent/tool/tool_delete_test.cc
)
target_include_directories(jiaolong_tool_delete_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(jiaolong_tool_delete_test PRIVATE
    jiaolong_agent_lib
    cpr::cpr
    GTest::GTest
    GTest::Main
)

add_executable(jiaolong_tool_move_test
    agent/tool/tool_move_test.cc
)
target_include_directories(jiaolong_tool_move_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(jiaolong_tool_move_test PRIVATE
    jiaolong_agent_lib
    cpr::cpr
    GTest::GTest
    GTest::Main
)

add_executable(jiaolong_tool_copy_test
    agent/tool/tool_copy_test.cc
)
target_include_directories(jiaolong_tool_copy_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(jiaolong_tool_copy_test PRIVATE
    jiaolong_agent_lib
    cpr::cpr
    GTest::GTest
    GTest::Main
)

add_executable(jiaolong_tool_edit_test
    agent/tool/tool_edit_test.cc
)
target_include_directories(jiaolong_tool_edit_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(jiaolong_tool_edit_test PRIVATE
    jiaolong_agent_lib
    cpr::cpr
    GTest::GTest
    GTest::Main
)

add_executable(jiaolong_permission_checker_test
    agent/permission_checker/claude_permission_checker_test.cc
    agent/permission_checker/bash_command_permission_checker_test.cc
    agent/permission_checker/mount_table_test.cc
    agent/permission_checker/permission_checker_test.cc
)
target_include_directories(jiaolong_permission_checker_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${JIAOLONG_PREBUILTS_PATH}/nlohmann_json/include"
)
target_link_libraries(jiaolong_permission_checker_test PRIVATE
    jiaolong_permission_checker_lib
    GTest::GTest
    GTest::Main
)

add_executable(jiaolong_session_history_visualizer_test
    agent/session_history_visualizer/session_history_visualizer_test.cc
)
target_include_directories(jiaolong_session_history_visualizer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(jiaolong_session_history_visualizer_test PRIVATE
    jiaolong_session_history_visualizer_lib
    GTest::GTest
    GTest::Main
)

add_executable(jiaolong_client_credential_manager_test
    server/client_credential_manager_test.cc
    server/client_credential_manager.cc
)
target_include_directories(jiaolong_client_credential_manager_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(jiaolong_client_credential_manager_test PRIVATE
    jiaolong_agent_lib
    cpr::cpr
    GTest::GTest
    GTest::Main
)

add_executable(jiaolong_task_service_test
    server/task_service_test.cc
    server/task_database.cc
    server/task_service.cc
    server/project_database.cc
)
target_include_directories(jiaolong_task_service_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${JIAOLONG_PREBUILTS_PATH}/sqlite"
)
target_link_libraries(jiaolong_task_service_test PRIVATE
    jiaolong_util_lib
    sqlite_lib
    nlohmann_json::nlohmann_json
    GTest::GTest
    GTest::Main
)

add_executable(jiaolong_server_test
    server/jiaolong_server_test.cc
    server/jiaolong_server.cc
    server/client_credential_manager.cc
    server/settings_service.cc
    server/system_health_data_manager.cc
    server/task_database.cc
    server/task_service.cc
    server/project_database.cc
    server/project_service.cc
)
target_include_directories(jiaolong_server_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${JIAOLONG_PREBUILTS_PATH}/nlohmann_json/include"
    "${JIAOLONG_PREBUILTS_PATH}/httplib/include"
    "${JIAOLONG_PREBUILTS_PATH}/sqlite"
)
target_link_libraries(jiaolong_server_test PRIVATE
    jiaolong_agent_lib
    jiaolong_util_lib
    sqlite_lib
    nlohmann_json::nlohmann_json
    Threads::Threads
    GTest::GTest
    GTest::Main
)

add_executable(jiaolong_system_health_data_manager_test
    server/system_health_data_manager_test.cc
    server/system_health_data_manager.cc
)
target_include_directories(jiaolong_system_health_data_manager_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${JIAOLONG_PREBUILTS_PATH}/nlohmann_json/include"
    "${JIAOLONG_PREBUILTS_PATH}/httplib/include"
)
target_link_libraries(jiaolong_system_health_data_manager_test PRIVATE
    nlohmann_json::nlohmann_json
    Threads::Threads
    GTest::GTest
    GTest::Main
)

include(GoogleTest)
gtest_discover_tests(jiaolong_agent_test)
gtest_discover_tests(jiaolong_tool_find_test)
gtest_discover_tests(jiaolong_tool_read_test)
gtest_discover_tests(jiaolong_tool_list_directory_test)
gtest_discover_tests(jiaolong_tool_delete_test)
gtest_discover_tests(jiaolong_tool_move_test)
gtest_discover_tests(jiaolong_tool_copy_test)
gtest_discover_tests(jiaolong_tool_edit_test)
gtest_discover_tests(jiaolong_permission_checker_test)
gtest_discover_tests(jiaolong_session_history_visualizer_test)
gtest_discover_tests(jiaolong_client_credential_manager_test)
gtest_discover_tests(jiaolong_task_service_test)
gtest_discover_tests(jiaolong_server_test)
gtest_discover_tests(jiaolong_system_health_data_manager_test)
