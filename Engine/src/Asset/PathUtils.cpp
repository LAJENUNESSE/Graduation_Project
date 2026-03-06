#include "engpch.h"
#include "Asset/PathUtils.h"

#include "Core/Log.h"

#include <system_error>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Engine::PathUtils
{
    namespace
    {
        std::filesystem::path s_ProjectRoot;

        std::filesystem::path NormalizePath(const std::filesystem::path& path)
        {
            std::error_code ec;
            auto normalized = path.lexically_normal();
            auto weak = std::filesystem::weakly_canonical(normalized, ec);
            return ec ? normalized : weak;
        }

        std::filesystem::path GetFallbackProjectRoot()
        {
#ifdef _WIN32
            wchar_t exePath[MAX_PATH];
            if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0)
                return NormalizePath(std::filesystem::path(exePath).parent_path());
#endif
            return {};
        }
    } // namespace

    std::string NormalizeSeparators(const std::string& path)
    {
        std::string result = path;
        for (auto& c : result)
        {
            if (c == '\\')
                c = '/';
        }
        return result;
    }

    bool IsSafeAssetPath(const std::string& path)
    {
        if (path.empty())
            return false;

        std::string normalized = NormalizeSeparators(path);
        std::filesystem::path filePath(normalized);
        if (filePath.is_absolute())
            return false;
        if (normalized.find("..") != std::string::npos)
            return false;
        return true;
    }

    bool IsLikelyURL(const std::string& path)
    {
        auto schemePos = path.find("://");
        return schemePos != std::string::npos && schemePos > 1;
    }

    void SetProjectRoot(const std::filesystem::path& projectRoot)
    {
        s_ProjectRoot = NormalizePath(projectRoot);
        ENGINE_CORE_INFO("[ProjectPaths] Project root = {0}", s_ProjectRoot.string());
    }

    bool DiscoverProjectRoot(const std::filesystem::path& startDirectory)
    {
        std::filesystem::path dir = NormalizePath(startDirectory);
        for (int i = 0; i < 12; ++i)
        {
            if (std::filesystem::exists(dir / "assets") &&
                std::filesystem::exists(dir / "Editor"))
            {
                SetProjectRoot(dir);
                return true;
            }

            auto parent = dir.parent_path();
            if (parent == dir)
                break;
            dir = parent;
        }

        return false;
    }

    bool HasProjectRoot()
    {
        return !s_ProjectRoot.empty();
    }

    const std::filesystem::path& GetProjectRoot()
    {
        if (s_ProjectRoot.empty())
        {
            static const std::filesystem::path fallback = GetFallbackProjectRoot();
            return fallback;
        }
        return s_ProjectRoot;
    }

    std::filesystem::path GetAssetRoot()
    {
        return GetProjectRoot() / "assets";
    }

    std::filesystem::path GetLogsRoot()
    {
        return GetProjectRoot() / "logs";
    }

    std::filesystem::path GetEditorAssetRoot()
    {
        return GetProjectRoot() / "Editor" / "assets";
    }

    std::filesystem::path ResolvePath(const std::filesystem::path& path)
    {
        if (path.empty())
            return {};
        if (path.is_absolute())
            return NormalizePath(path);
        return NormalizePath(GetProjectRoot() / path);
    }

    std::string ResolvePathString(const std::string& path)
    {
        if (path.empty() || IsLikelyURL(path) || path.rfind("builtin:", 0) == 0)
            return path;
        return ResolvePath(path).string();
    }

    bool TryToProjectRelative(const std::filesystem::path& path, std::string& outPath)
    {
        if (path.empty())
            return false;

        std::filesystem::path absolutePath = ResolvePath(path);
        std::error_code ec;
        std::filesystem::path relative = std::filesystem::relative(absolutePath, GetProjectRoot(), ec);
        if (ec)
            return false;

        std::string normalized = NormalizeSeparators(relative.string());
        if (!IsSafeAssetPath(normalized))
            return false;

        outPath = normalized;
        return true;
    }

    std::string ToProjectRelativeOrAbsolute(const std::filesystem::path& path)
    {
        std::string relativePath;
        if (TryToProjectRelative(path, relativePath))
            return relativePath;
        return NormalizeSeparators(ResolvePath(path).string());
    }

} // namespace Engine::PathUtils



