#pragma once
#include <string>
#include <filesystem>

namespace Engine { namespace PathUtils {

    // 反斜杠 → 正斜杠，解决 Windows std::filesystem::relative().string() 产生 '\' 的问题
    inline std::string NormalizeSeparators(const std::string& path)
    {
        std::string result = path;
        for (auto& c : result)
            if (c == '\\') c = '/';
        return result;
    }

    // 统一路径安全校验：拒绝绝对路径 + 目录穿越
    // 先规范化再检查，因此 Windows 反斜杠路径不再被误拒
    inline bool IsSafeAssetPath(const std::string& path)
    {
        if (path.empty()) return false;
        std::string normalized = NormalizeSeparators(path);
        std::filesystem::path p(normalized);
        if (p.is_absolute()) return false;
        if (normalized.find("..") != std::string::npos) return false;
        return true;
    }

}} // namespace Engine::PathUtils
