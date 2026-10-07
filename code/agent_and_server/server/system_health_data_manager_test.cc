#include <chrono>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "server/system_health_data_manager.h"

namespace jiaolong {
namespace server {
namespace {

// Short loop interval used in tests so frames are gathered quickly without
// slowing down the test suite.
constexpr std::chrono::milliseconds kTestLoopInterval(20);

// Domain passed to the manager in tests. The injected readers ignore it.
constexpr char kTestMacSystemHealthDataServerDomain[] = "127.0.0.1";

TEST(SystemHealthDataManagerTest, PerceptionLoopGathersFramesFromReader) {
  SystemHealthDataManager manager(
      kTestMacSystemHealthDataServerDomain, kTestLoopInterval,
      []() -> std::optional<double> { return 42.5; });
  manager.Start();
  // Give the perception loop enough time to gather a few frames.
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  manager.Stop();

  const std::vector<SystemHealthDataFrame> frames =
      manager.GetLastFramesVector(10);
  ASSERT_FALSE(frames.empty());
  const SystemHealthDataFrame& latest = frames.back();
  EXPECT_DOUBLE_EQ(latest.cpu_temperature_celsius, 42.5);
  EXPECT_GT(latest.timestamp_ms, 0);
}

TEST(SystemHealthDataManagerTest, GetLastFramesReturnsJsonArrayWithFrameFields) {
  SystemHealthDataManager manager(
      kTestMacSystemHealthDataServerDomain, kTestLoopInterval,
      []() -> std::optional<double> { return 55.0; });
  manager.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  manager.Stop();

  const nlohmann::json frames = manager.GetLastFrames(10);
  ASSERT_TRUE(frames.is_array());
  ASSERT_FALSE(frames.empty());
  const nlohmann::json& latest = frames.back();
  EXPECT_TRUE(latest.contains("timestamp"));
  EXPECT_TRUE(latest.contains("cpuTemperatureCelsius"));
  EXPECT_DOUBLE_EQ(latest["cpuTemperatureCelsius"].get<double>(), 55.0);
}

TEST(SystemHealthDataManagerTest, GetLastFramesLimitsCountAndOldestFirst) {
  SystemHealthDataManager manager(
      kTestMacSystemHealthDataServerDomain, kTestLoopInterval,
      []() -> std::optional<double> { return 50.0; });
  manager.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  manager.Stop();

  // Request fewer frames than were gathered and verify the JSON array is
  // ordered oldest first and respects the requested count.
  const nlohmann::json frames = manager.GetLastFrames(3);
  ASSERT_TRUE(frames.is_array());
  ASSERT_EQ(frames.size(), 3u);
  EXPECT_LE(frames[0]["timestamp"].get<int64_t>(),
            frames[2]["timestamp"].get<int64_t>());
}

TEST(SystemHealthDataManagerTest, FailedReaderTreatsAsNoData) {
  // A reader that returns std::nullopt simulates a failed request to the mac
  // system health data server; the manager must handle it gracefully and keep
  // no data.
  SystemHealthDataManager manager(
      kTestMacSystemHealthDataServerDomain, kTestLoopInterval,
      []() -> std::optional<double> { return std::nullopt; });
  manager.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  manager.Stop();

  EXPECT_TRUE(manager.GetLastFrames(10).empty());
  EXPECT_TRUE(manager.GetLastFramesVector(10).empty());
}

TEST(SystemHealthDataManagerTest, UnstartedManagerReturnsNoFrames) {
  SystemHealthDataManager manager(
      kTestMacSystemHealthDataServerDomain, kTestLoopInterval,
      []() -> std::optional<double> { return 50.0; });
  EXPECT_FALSE(manager.IsRunning());
  EXPECT_TRUE(manager.GetLastFrames(10).empty());
  EXPECT_TRUE(manager.GetLastFramesVector(10).empty());
  // Stop() must be safe when the loop has never been started.
  manager.Stop();
}

TEST(SystemHealthDataManagerTest, StartIsIdempotentAndStopStopsTheLoop) {
  SystemHealthDataManager manager(
      kTestMacSystemHealthDataServerDomain, kTestLoopInterval,
      []() -> std::optional<double> { return 50.0; });
  manager.Start();
  manager.Start();  // second start must be a no-op
  EXPECT_TRUE(manager.IsRunning());
  manager.Stop();
  EXPECT_FALSE(manager.IsRunning());

  const size_t frames_after_stop = manager.GetLastFramesVector(1000).size();
  // Stop() must be safe to call again and no new frame may be gathered after
  // the loop stopped.
  manager.Stop();
  EXPECT_EQ(manager.GetLastFramesVector(1000).size(), frames_after_stop);
}

}  // namespace
}  // namespace server
}  // namespace jiaolong
