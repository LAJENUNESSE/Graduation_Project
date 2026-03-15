#pragma once

#include <filesystem>
#include <string>

namespace Engine
{

    enum class AssetType
    {
        None = 0,
        Texture2D,
        TextureCubemap,
        Mesh,
        Scene,
        Shader,
        Audio,
        Video
    };

    // 根据文件路径的扩展名推断 AssetType（替代各种 IsXxxFile()）
    AssetType AssetTypeFromPath(const std::filesystem::path& path);

    AssetType AssetTypeFromExtension(const std::string& ext);
    const char* AssetTypeToString(AssetType type);

} // namespace Engine
