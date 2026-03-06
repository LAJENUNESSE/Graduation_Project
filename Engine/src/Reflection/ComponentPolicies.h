#pragma once

#include <array>
#include <string_view>

namespace Engine::ComponentPolicies
{
    namespace Detail
    {
        template <size_t N>
        constexpr bool Contains(std::string_view value, const std::array<std::string_view, N>& items)
        {
            for (std::string_view item : items)
            {
                if (item == value)
                    return true;
            }
            return false;
        }
    } // namespace Detail

    inline bool IsCustomCopyComponentType(std::string_view typeName)
    {
        constexpr std::array<std::string_view, 12> kCustomCopyTypes = {
            "IDComponent",
            "TagComponent",
            "TransformComponent",
            "RelationshipComponent",
            "MeshRendererComponent",
            "CameraComponent",
            "TerrainComponent",
            "ParticleEmitterComponent",
            "NativeScriptComponent",
            "AudioSourceComponent",
            "AudioListenerComponent",
            "VideoPlayerComponent"
        };
        return Detail::Contains(typeName, kCustomCopyTypes);
    }

    inline bool IsCustomAddMenuComponentType(std::string_view typeName)
    {
        constexpr std::array<std::string_view, 8> kCustomAddMenuTypes = {
            "CameraComponent",
            "MeshRendererComponent",
            "TerrainComponent",
            "ParticleEmitterComponent",
            "NativeScriptComponent",
            "AudioSourceComponent",
            "AudioListenerComponent",
            "VideoPlayerComponent"
        };
        return Detail::Contains(typeName, kCustomAddMenuTypes);
    }

    inline bool IsCustomInspectorComponentType(std::string_view typeName)
    {
        constexpr std::array<std::string_view, 12> kCustomInspectorTypes = {
            "IDComponent",
            "TagComponent",
            "TransformComponent",
            "RelationshipComponent",
            "CameraComponent",
            "MeshRendererComponent",
            "TerrainComponent",
            "ParticleEmitterComponent",
            "NativeScriptComponent",
            "AudioSourceComponent",
            "AudioListenerComponent",
            "VideoPlayerComponent"
        };
        return Detail::Contains(typeName, kCustomInspectorTypes);
    }

} // namespace Engine::ComponentPolicies