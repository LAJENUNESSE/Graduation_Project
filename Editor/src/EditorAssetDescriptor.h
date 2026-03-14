#pragma once

#include "Asset/AssetType.h"

namespace Engine
{

    // Editor 层资源描述符 — 定义 UI 图标和拖拽 payload 类型
    struct EditorAssetDescriptor
    {
        const char* Icon;        // "[I]", "[S]" 等
        const char* PayloadType; // "ASSET_TEXTURE" 等
    };

    // 根据 AssetType 获取 Editor 层的 UI 描述符
    const EditorAssetDescriptor& GetEditorAssetDescriptor(AssetType type);

} // namespace Engine
