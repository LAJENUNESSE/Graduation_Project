#pragma once

#include <filesystem>
#include <string>

namespace Engine::PathUtils
{

    std::string           NormalizeSeparators(const std::string& path);
    std::filesystem::path PathFromUtf8(const std::string& path);
    std::string           PathToUtf8String(const std::filesystem::path& path);

    bool IsSafeAssetPath(const std::string& path);
    bool IsLikelyURL(const std::string& path);

    void SetProjectRoot(const std::filesystem::path& projectRoot);
    bool DiscoverProjectRoot(const std::filesystem::path& startDirectory);
    bool HasProjectRoot();

    const std::filesystem::path& GetProjectRoot();
    std::filesystem::path        GetAssetRoot();
    std::filesystem::path        GetLogsRoot();
    std::filesystem::path        GetEditorAssetRoot();

    std::filesystem::path ResolvePath(const std::filesystem::path& path);
    std::filesystem::path ResolvePath(const std::string& path);
    std::string           ResolvePathString(const std::string& path);

    bool        TryToProjectRelative(const std::filesystem::path& path, std::string& outPath);
    std::string ToProjectRelativeOrAbsolute(const std::filesystem::path& path);

} // namespace Engine::PathUtils
