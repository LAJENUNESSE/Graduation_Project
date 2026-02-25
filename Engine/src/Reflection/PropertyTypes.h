#pragma once

#include <string>

namespace Engine
{

    enum class PropertyType
    {
        Float,
        Int,
        UInt32,
        Bool,
        String,
        Vec2,
        Vec3,
        Vec4,
        Color3,
        Color4,
        Enum,
        AssetPath
    };

    struct PropertyHints
    {
        float Min = 0.0f;
        float Max = 0.0f;
        float Speed = 0.0f;
        const char* Format = nullptr;
        const char* Group = nullptr;
        bool ReadOnly = false;
        bool Transient = false;

        // Enum
        const char* const* EnumNames = nullptr;
        int EnumCount = 0;

        // AssetPath
        const char* FileFilter = nullptr;
        const char* FileDesc = nullptr;
    };

} // namespace Engine
