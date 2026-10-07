#pragma once

#include <filesystem>
#include <vector>

namespace jiaolong {

bool IsSameOrSubDirectory(const std::filesystem::path& child,
    const std::filesystem::path& parent);

bool IsPathAllowed(const std::filesystem::path& path,
    const std::vector<std::filesystem::path>& allowed_paths);

std::filesystem::path ToAbsolutePath(const std::filesystem::path& path,
    const std::filesystem::path& working_directory);

}  // namespace jiaolong
