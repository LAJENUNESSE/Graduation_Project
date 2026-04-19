#include "engpch.h"
#include "Asset/AssetType.h"

#include <algorithm>
#include <unordered_map>

namespace Engine
{

    // ── 集中式扩展名 → AssetType 注册表 ──────────────────────────
    static const std::unordered_map<std::string, AssetType> s_ExtensionMap = {
        // 纹理
        {".png", AssetType::Texture2D},
        {".jpg", AssetType::Texture2D},
        {".jpeg", AssetType::Texture2D},
        {".bmp", AssetType::Texture2D},
        {".tga", AssetType::Texture2D},
        {".hdr", AssetType::Texture2D},
        // 模型
        {".obj", AssetType::Mesh},
        {".fbx", AssetType::Mesh},
        {".gltf", AssetType::Mesh},
        {".glb", AssetType::Mesh},
        // 场景
        {".scene", AssetType::Scene},
        // 着色器
        {".glsl", AssetType::Shader},
        // 音频
        {".wav", AssetType::Audio},
        {".mp3", AssetType::Audio},
        {".ogg", AssetType::Audio},
        // 视频
        {".mp4", AssetType::Video},
        {".avi", AssetType::Video},
        {".mkv", AssetType::Video},
        // 脚本
        {".lua", AssetType::Script},
    };

    AssetType AssetTypeFromPath(const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        auto it = s_ExtensionMap.find(ext);
        if (it != s_ExtensionMap.end())
            return it->second;
        return AssetType::None;
    }

    AssetType AssetTypeFromExtension(const std::string& ext)
    {
        std::string lower = ext;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        auto it = s_ExtensionMap.find(lower);
        if (it != s_ExtensionMap.end())
            return it->second;
        return AssetType::None;
    }

    const char* AssetTypeToString(AssetType type)
    {
        switch (type)
        {
        case AssetType::Texture2D:
            return "Texture2D";
        case AssetType::TextureCubemap:
            return "TextureCubemap";
        case AssetType::Mesh:
            return "Mesh";
        case AssetType::Scene:
            return "Scene";
        case AssetType::Shader:
            return "Shader";
        case AssetType::Audio:
            return "Audio";
        case AssetType::Video:
            return "Video";
        case AssetType::Script:
            return "Script";
        default:
            return "None";
        }
    }

} // namespace Engine
