#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "agent/permission_checker/permission_checker.h"

namespace jiaolong {
namespace {

class PermissionCheckerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static int test_id = 0;
    root_ = std::filesystem::temp_directory_path() /
        ("jiaolong_permission_checker_test_" + std::to_string(++test_id));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_ / "workspace" / "src");
    std::filesystem::create_directories(root_ / "docs");
    std::filesystem::create_directories(root_ / "outside");
    {
      std::ofstream file(root_ / "workspace" / "src" / "main.cc");
      file << "int main() {}\n";
    }
    {
      std::ofstream file(root_ / "workspace" / "readonly.txt");
      file << "keep me\n";
    }
    {
      std::ofstream file(root_ / "docs" / "api.md");
      file << "# API\n";
    }
  }

  void TearDown() override {
    std::filesystem::remove_all(root_);
  }

  Project BuildProject() const {
    Project project;
    project.id_ = "project_1";
    ReadonlyDirectory docs;
    docs.id_ = "dir_docs";
    docs.alias_ = "docs";
    docs.real_path_ = (root_ / "docs").string();
    project.readonly_directories_ = {docs};
    return project;
  }

  Task BuildTask() const {
    return Task("task_1", "title", "description",
        (root_ / "workspace").string(), /*token_limit=*/1000, {"dir_docs"});
  }

  PermissionChecker BuildChecker(
      const std::vector<std::string>& forbidden = {}) const {
    PermissionContext context;
    context.mount_table = MountTable::FromTask(BuildTask(), BuildProject());
    context.forbidden_to_write = forbidden;
    return PermissionChecker(std::move(context));
  }

  std::filesystem::path root_;
};

TEST_F(PermissionCheckerTest, ReadToolsAreAllowedOnRwAndRoMounts) {
  const PermissionChecker checker = BuildChecker();

  const PermissionCheckResult rw = checker.CheckToolCall("ReadWholeFile",
      nlohmann::json{{"filePath", "/workspace/src/main.cc"}});
  EXPECT_TRUE(rw.allowed);
  ASSERT_EQ(rw.real_paths.size(), 1u);

  const PermissionCheckResult ro = checker.CheckToolCall("ReadWholeFile",
      nlohmann::json{{"filePath", "/readonly/docs/api.md"}});
  EXPECT_TRUE(ro.allowed);
  ASSERT_EQ(ro.real_paths.size(), 1u);
}

TEST_F(PermissionCheckerTest, WriteToolsAreAllowedOnRwMountsOnly) {
  const PermissionChecker checker = BuildChecker();

  const PermissionCheckResult rw = checker.CheckToolCall("WriteContentToFile",
      nlohmann::json{{"filePath", "/workspace/new.txt"}, {"content", "x"}});
  EXPECT_TRUE(rw.allowed);

  const PermissionCheckResult ro = checker.CheckToolCall("WriteContentToFile",
      nlohmann::json{{"filePath", "/readonly/docs/api.md"}, {"content", "x"}});
  EXPECT_FALSE(ro.allowed);
  EXPECT_EQ(ro.reason, "forbidden file path");
}

TEST_F(PermissionCheckerTest, CopyAllowsRoToRwOnly) {
  const PermissionChecker checker = BuildChecker();

  const PermissionCheckResult ro_to_rw = checker.CheckToolCall("Copy",
      nlohmann::json{{"sourcePath", "/readonly/docs/api.md"},
                     {"destinationPath", "/workspace/api.md"}});
  EXPECT_TRUE(ro_to_rw.allowed);

  const PermissionCheckResult rw_to_ro = checker.CheckToolCall("Copy",
      nlohmann::json{{"sourcePath", "/workspace/src/main.cc"},
                     {"destinationPath", "/readonly/docs/main.cc"}});
  EXPECT_FALSE(rw_to_ro.allowed);
  EXPECT_EQ(rw_to_ro.reason, "forbidden file path");

  const PermissionCheckResult ro_to_ro = checker.CheckToolCall("Copy",
      nlohmann::json{{"sourcePath", "/readonly/docs/api.md"},
                     {"destinationPath", "/readonly/docs/api2.md"}});
  EXPECT_FALSE(ro_to_ro.allowed);
}

TEST_F(PermissionCheckerTest, MoveRequiresBothEndsReadWrite) {
  const PermissionChecker checker = BuildChecker();

  const PermissionCheckResult rw_to_rw = checker.CheckToolCall("Move",
      nlohmann::json{{"sourcePath", "/workspace/src/main.cc"},
                     {"destinationPath", "/workspace/main.cc"}});
  EXPECT_TRUE(rw_to_rw.allowed);

  const PermissionCheckResult ro_to_rw = checker.CheckToolCall("Move",
      nlohmann::json{{"sourcePath", "/readonly/docs/api.md"},
                     {"destinationPath", "/workspace/api.md"}});
  EXPECT_FALSE(ro_to_rw.allowed);

  const PermissionCheckResult rw_to_ro = checker.CheckToolCall("Move",
      nlohmann::json{{"sourcePath", "/workspace/src/main.cc"},
                     {"destinationPath", "/readonly/docs/main.cc"}});
  EXPECT_FALSE(rw_to_ro.allowed);
}

TEST_F(PermissionCheckerTest, ForbiddenToWriteFileListIsEnforcedOnRwMount) {
  const std::string forbidden_path =
      std::filesystem::weakly_canonical(root_ / "workspace" / "readonly.txt")
          .string();
  const PermissionChecker checker = BuildChecker({forbidden_path});

  const PermissionCheckResult denied = checker.CheckToolCall("WriteContentToFile",
      nlohmann::json{{"filePath", "/workspace/readonly.txt"},
                     {"content", "hacked"}});
  EXPECT_FALSE(denied.allowed);
  EXPECT_EQ(denied.reason, "forbidden file path");

  // Read tools are never checked against the forbidden-to-write list.
  const PermissionCheckResult allowed = checker.CheckToolCall("ReadWholeFile",
      nlohmann::json{{"filePath", "/workspace/readonly.txt"}});
  EXPECT_TRUE(allowed.allowed);
}

TEST_F(PermissionCheckerTest, InvalidPathsAndUnknownToolsAreDenied) {
  const PermissionChecker checker = BuildChecker();

  const PermissionCheckResult outside = checker.CheckToolCall("ReadWholeFile",
      nlohmann::json{{"filePath", "/etc/passwd"}});
  EXPECT_FALSE(outside.allowed);
  EXPECT_EQ(outside.reason, "invalid path");

  const PermissionCheckResult unknown = checker.CheckToolCall("TotallyNotATool",
      nlohmann::json{{"filePath", "/workspace/src/main.cc"}});
  EXPECT_FALSE(unknown.allowed);
}

}  // namespace
}  // namespace jiaolong