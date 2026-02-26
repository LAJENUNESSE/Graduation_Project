#pragma once

#include <string>

namespace Engine
{

    enum class AssetType
    {
        None = 0,
        Texture2D,
        TextureCubemap,
        Mesh
    };

    AssetType AssetTypeFromExtension(const std::string& ext);
    const char* AssetTypeToString(AssetType type);

} // namespace Engine
