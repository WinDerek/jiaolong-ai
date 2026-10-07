#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "agent/permission_checker/mount_table.h"

namespace jiaolong {
namespace {

class MountTableTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static int test_id = 0;
    root_ = std::filesystem::temp_directory_path() /
        ("jiaolong_mount_table_test_" + std::to_string(++test_id));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_ / "workspace");
    std::filesystem::create_directories(root_ / "workspace" / "src");
    std::filesystem::create_directories(root_ / "docs");
    std::filesystem::create_directories(root_ / "libs");
    std::filesystem::create_directories(root_ / "outside");
    {
      std::ofstream file(root_ / "workspace" / "src" / "main.cc");
      file << "int main() {}\n";
    }
    {
      std::ofstream file(root_ / "docs" / "api.md");
      file << "# API\n";
    }
    {
      std::ofstream file(root_ / "outside" / "secret.txt");
      file << "TOP SECRET\n";
    }
  }

  void TearDown() override {
    std::filesystem::remove_all(root_);
  }

  // Builds a project whose catalog is `docs` -> <root>/docs and
  // `libs` -> <root>/libs.
  Project BuildProject() const {
    Project project;
    project.id_ = "project_1";
    ReadonlyDirectory docs;
    docs.id_ = "dir_docs";
    docs.alias_ = "docs";
    docs.real_path_ = (root_ / "docs").string();
    docs.description_ = "Public API docs";
    ReadonlyDirectory libs;
    libs.id_ = "dir_libs";
    libs.alias_ = "libs";
    libs.real_path_ = (root_ / "libs").string();
    libs.description_ = "Shared library headers";
    project.readonly_directories_ = {docs, libs};
    return project;
  }

  Task BuildTask(const std::vector<std::string>& selected) const {
    return Task("task_1", "title", "description",
        (root_ / "workspace").string(), /*token_limit=*/1000, selected);
  }

  std::filesystem::path root_;
};

std::string Canonical(const std::filesystem::path& path) {
  return std::filesystem::weakly_canonical(path).string();
}

TEST_F(MountTableTest, WorkspaceOnlyMountWhenNoReadonlyDirectorySelected) {
  MountTable table = MountTable::FromTask(BuildTask({}), BuildProject());
  ASSERT_EQ(table.mounts().size(), 1u);
  EXPECT_EQ(table.readonly_mount_count(), 0);

  const std::optional<ResolvedPath> resolved =
      table.ToRealPath("/workspace/src/main.cc");
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->real_path.string(),
      Canonical(root_ / "workspace" / "src" / "main.cc"));
  EXPECT_EQ(resolved->access, MountAccess::kReadWrite);

  // An unselected project readonly directory is not a mount.
  EXPECT_FALSE(table.ToRealPath("/readonly/docs/api.md").has_value());
}

TEST_F(MountTableTest, SelectedReadonlyDirectoriesBecomeReadOnlyMounts) {
  MountTable table =
      MountTable::FromTask(BuildTask({"dir_docs", "dir_libs"}), BuildProject());
  ASSERT_EQ(table.mounts().size(), 3u);
  EXPECT_EQ(table.readonly_mount_count(), 2);

  const std::optional<ResolvedPath> docs =
      table.ToRealPath("/readonly/docs/api.md");
  ASSERT_TRUE(docs.has_value());
  EXPECT_EQ(docs->real_path.string(), Canonical(root_ / "docs" / "api.md"));
  EXPECT_EQ(docs->access, MountAccess::kReadOnly);

  const std::optional<ResolvedPath> libs = table.ToRealPath("/readonly/libs");
  ASSERT_TRUE(libs.has_value());
  EXPECT_EQ(libs->real_path.string(), Canonical(root_ / "libs"));
  EXPECT_EQ(libs->access, MountAccess::kReadOnly);
}

TEST_F(MountTableTest, UnselectedReadonlyDirectoryIsNotResolvable) {
  // Only `docs` is selected: `/readonly/libs/...` must be an unknown alias.
  MountTable table = MountTable::FromTask(BuildTask({"dir_docs"}), BuildProject());
  ASSERT_TRUE(table.ToRealPath("/readonly/docs/api.md").has_value());
  EXPECT_FALSE(table.ToRealPath("/readonly/libs/api.md").has_value());
}

TEST_F(MountTableTest, UnknownReadonlyDirectoryIdIsIgnored) {
  MountTable table =
      MountTable::FromTask(BuildTask({"does_not_exist"}), BuildProject());
  EXPECT_EQ(table.mounts().size(), 1u);
}

TEST_F(MountTableTest, RejectsUnknownAndNonAbsoluteRoots) {
  MountTable table =
      MountTable::FromTask(BuildTask({"dir_docs"}), BuildProject());
  EXPECT_FALSE(table.ToRealPath("/etc/passwd").has_value());
  EXPECT_FALSE(table.ToRealPath("/workspace_evil/secret.txt").has_value());
  EXPECT_FALSE(table.ToRealPath("workspace/secret.txt").has_value());
  EXPECT_FALSE(table.ToRealPath("relative.txt").has_value());
  EXPECT_FALSE(table.ToRealPath("/readonly/unknown/a.md").has_value());
}

TEST_F(MountTableTest, RejectsPathsEscapingTheMountRoot) {
  MountTable table =
      MountTable::FromTask(BuildTask({"dir_docs"}), BuildProject());
  EXPECT_FALSE(table.ToRealPath("/workspace/../outside/secret.txt").has_value());
  EXPECT_FALSE(
      table.ToRealPath("/readonly/docs/../../outside/secret.txt").has_value());
}

TEST_F(MountTableTest, RejectsSymlinkEscapingTheMountRoot) {
  std::error_code error_code;
  std::filesystem::create_symlink(
      root_ / "outside" / "secret.txt", root_ / "workspace" / "link.txt",
      error_code);
  if (error_code) {
    GTEST_SKIP() << "symlinks are not supported on this platform";
  }

  MountTable table = MountTable::FromTask(BuildTask({}), BuildProject());
  EXPECT_FALSE(table.ToRealPath("/workspace/link.txt").has_value());
}

TEST_F(MountTableTest, ToVirtualPathRoundTripsToRealPath) {
  MountTable table =
      MountTable::FromTask(BuildTask({"dir_docs", "dir_libs"}), BuildProject());

  const std::vector<std::string> virtual_paths = {
      "/workspace/src/main.cc",
      "/readonly/docs/api.md",
      "/readonly/libs",
  };
  for (const std::string& virtual_path : virtual_paths) {
    const std::optional<ResolvedPath> resolved = table.ToRealPath(virtual_path);
    ASSERT_TRUE(resolved.has_value()) << virtual_path;
    const std::optional<std::string> round_tripped =
        table.ToVirtualPath(resolved->real_path);
    ASSERT_TRUE(round_tripped.has_value()) << virtual_path;
    EXPECT_EQ(*round_tripped, virtual_path);
  }
}

TEST_F(MountTableTest, ToVirtualPathPrefersTheDeepestRealRoot) {
  // A readonly directory nested inside the working directory: both mounts
  // could map the real path, and the deepest (longest) one must win.
  std::filesystem::create_directories(root_ / "workspace" / "nested");
  Project project = BuildProject();
  ReadonlyDirectory nested;
  nested.id_ = "dir_nested";
  nested.alias_ = "nested";
  nested.real_path_ = (root_ / "workspace" / "nested").string();
  project.readonly_directories_.push_back(nested);

  MountTable table = MountTable::FromTask(BuildTask({"dir_nested"}), project);
  const std::optional<std::string> virtual_path =
      table.ToVirtualPath(root_ / "workspace" / "nested" / "file.txt");
  ASSERT_TRUE(virtual_path.has_value());
  EXPECT_EQ(*virtual_path, "/readonly/nested/file.txt");
}

TEST_F(MountTableTest, ToVirtualPathRejectsPathsOutsideAllMounts) {
  MountTable table = MountTable::FromTask(BuildTask({}), BuildProject());
  EXPECT_FALSE(table.ToVirtualPath(root_ / "outside" / "secret.txt").has_value());
}

TEST_F(MountTableTest, AliasValidation) {
  EXPECT_TRUE(IsValidReadonlyDirectoryAlias("docs"));
  EXPECT_TRUE(IsValidReadonlyDirectoryAlias("libs-1_2"));
  EXPECT_FALSE(IsValidReadonlyDirectoryAlias(""));
  EXPECT_FALSE(IsValidReadonlyDirectoryAlias("workspace"));
  EXPECT_FALSE(IsValidReadonlyDirectoryAlias("../etc"));
  EXPECT_FALSE(IsValidReadonlyDirectoryAlias("with/slash"));
  EXPECT_FALSE(IsValidReadonlyDirectoryAlias("with space"));
  EXPECT_FALSE(IsValidReadonlyDirectoryAlias("with.dot"));
}

TEST_F(MountTableTest, InvalidAliasIsDiscarded) {
  Project project = BuildProject();
  ReadonlyDirectory invalid;
  invalid.id_ = "dir_bad";
  invalid.alias_ = "../bad";
  invalid.real_path_ = (root_ / "docs").string();
  project.readonly_directories_.push_back(invalid);

  MountTable table = MountTable::FromTask(BuildTask({"dir_bad"}), project);
  EXPECT_EQ(table.mounts().size(), 1u);
}

TEST_F(MountTableTest, SingleWorkspaceMountBehavesLikeThePrimaryMount) {
  MountTable table =
      MountTable::SingleWorkspaceMount((root_ / "workspace").string());
  ASSERT_EQ(table.mounts().size(), 1u);
  const std::optional<ResolvedPath> resolved =
      table.ToRealPath("/workspace/src/main.cc");
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->access, MountAccess::kReadWrite);
  EXPECT_EQ(resolved->real_path.string(),
      Canonical(root_ / "workspace" / "src" / "main.cc"));
}

}  // namespace
}  // namespace jiaolong