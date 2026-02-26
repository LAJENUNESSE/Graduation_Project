#include "engpch.h"
#include "Asset/AssetType.h"

#include <algorithm>

namespace Engine
{

    AssetType AssetTypeFromExtension(const std::string& ext)
    {
        std::string lower = ext;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower == ".png" || lower == ".jpg" || lower == ".jpeg" ||
            lower == ".bmp" || lower == ".tga" || lower == ".hdr")
            return AssetType::Texture2D;

        if (lower == ".obj" || lower == ".fbx" || lower == ".gltf" || lower == ".glb")
            return AssetType::Mesh;

        return AssetType::None;
    }

    const char* AssetTypeToString(AssetType type)
    {
        switch (type)
        {
        case AssetType::Texture2D:      return "Texture2D";
        case AssetType::TextureCubemap: return "TextureCubemap";
        case AssetType::Mesh:           return "Mesh";
        default:                        return "None";
        }
    }

} // namespace Engine
