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

#ifdef _WIN32
        std::wstring Utf8ToWide(const std::string& utf8)
        {
            if (utf8.empty())
                return {};

            const int utf8Size = static_cast<int>(utf8.size());
            int wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), utf8Size, nullptr, 0);
            UINT codePage = CP_UTF8;
            DWORD flags = MB_ERR_INVALID_CHARS;

            if (wideSize <= 0)
            {
                codePage = CP_ACP;
                flags = 0;
                wideSize = MultiByteToWideChar(codePage, flags, utf8.c_str(), utf8Size, nullptr, 0);
            }

            if (wideSize <= 0)
                return {};

            std::wstring wide(static_cast<size_t>(wideSize), L'\0');
            const int converted = MultiByteToWideChar(codePage, flags, utf8.c_str(), utf8Size, wide.data(), wideSize);
            if (converted <= 0)
                return {};

            return wide;
        }

        std::string WideToUtf8(const std::wstring& wide)
        {
            if (wide.empty())
                return {};

            const int wideSize = static_cast<int>(wide.size());
            int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideSize, nullptr, 0, nullptr, nullptr);
            if (utf8Size <= 0)
                return {};

            std::string utf8(static_cast<size_t>(utf8Size), '\0');
            const int converted = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideSize, utf8.data(), utf8Size, nullptr, nullptr);
            if (converted <= 0)
                return {};

            return utf8;
        }
#endif

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

    std::filesystem::path PathFromUtf8(const std::string& path)
    {
#ifdef _WIN32
        const std::wstring wide = Utf8ToWide(path);
        return wide.empty() ? std::filesystem::path() : std::filesystem::path(wide);
#else
        return std::filesystem::path(path);
#endif
    }

    std::string PathToUtf8String(const std::filesystem::path& path)
    {
#ifdef _WIN32
        return NormalizeSeparators(WideToUtf8(path.wstring()));
#else
        return NormalizeSeparators(path.string());
#endif
    }

    bool IsSafeAssetPath(const std::string& path)
    {
        if (path.empty())
            return false;

        std::string normalized = NormalizeSeparators(path);
        std::filesystem::path filePath = PathFromUtf8(normalized);
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
        if (Log::IsInitialized())
            ENGINE_CORE_INFO("[ProjectPaths] Project root = {0}", PathToUtf8String(s_ProjectRoot));
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

    std::filesystem::path ResolvePath(const std::string& path)
    {
        if (path.empty())
            return {};
        return ResolvePath(PathFromUtf8(path));
    }

    std::string ResolvePathString(const std::string& path)
    {
        if (path.empty() || IsLikelyURL(path) || path.rfind("builtin:", 0) == 0)
            return path;
        return PathToUtf8String(ResolvePath(path));
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

        std::string normalized = NormalizeSeparators(PathToUtf8String(relative));
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
        return PathToUtf8String(ResolvePath(path));
    }

} // namespace Engine::PathUtils
