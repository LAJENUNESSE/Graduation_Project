#include "EditorAssetDescriptor.h"

namespace Engine
{

    static const EditorAssetDescriptor s_NoneDescriptor = {"[F]", "ASSET_PATH"};

    static const struct
    {
        AssetType Type;
        EditorAssetDescriptor Descriptor;
    } s_Descriptors[] = {
        {AssetType::Texture2D, {"[I]", "ASSET_TEXTURE"}},
        {AssetType::TextureCubemap, {"[I]", "ASSET_TEXTURE"}},
        {AssetType::Mesh, {"[M]", "ASSET_MODEL"}},
        {AssetType::Scene, {"[S]", "ASSET_SCENE"}},
        {AssetType::Shader, {"[G]", "ASSET_SHADER"}},
        {AssetType::Audio, {"[A]", "ASSET_AUDIO"}},
        {AssetType::Video, {"[V]", "ASSET_VIDEO"}},
    };

    const EditorAssetDescriptor& GetEditorAssetDescriptor(AssetType type)
    {
        for (const auto& entry : s_Descriptors)
        {
            if (entry.Type == type)
                return entry.Descriptor;
        }
        return s_NoneDescriptor;
    }

} // namespace Engine
