#pragma once

#include "Core/Base.h"
#include "Renderer/PostProcessing.h"

#include <array>
#include <cstdlib>
#include <string>

namespace Engine
{

    class Scene;

    // 编辑器级别的渲染设置（不属于场景，但需要跟场景一起保存）
    struct EditorRenderSettings
    {
        PostProcessingSettings PostProcessing;
        uint32_t               MSAASamples    = 1;

        // SSAO 设置
        bool  SSAOEnabled    = false;
        float SSAORadius     = 0.5f;
        float SSAOBias       = 0.025f;
        int   SSAOKernelSize = 32;
        float SSAOIntensity  = 1.5f;
    };

    namespace EditorRenderSettingDomains
    {
        inline constexpr std::array<int, 6>         ShadowMapResolutions      = {256, 512, 1024, 2048, 4096, 8192};
        inline constexpr std::array<const char*, 6> ShadowMapResolutionLabels = {"256",  "512",  "1024",
                                                                                 "2048", "4096", "8192"};
        inline constexpr std::array<uint32_t, 4>    MSAASamples               = {1u, 2u, 4u, 8u};
        inline constexpr std::array<const char*, 4> MSAASampleLabels          = {"关闭", "2x", "4x", "8x"};
        inline constexpr std::array<int, 2>         ToneMappingModes          = {0, 1};
        inline constexpr std::array<const char*, 2> ToneMappingModeLabels     = {"Reinhard", "ACES"};

        inline int NormalizeShadowMapResolution(int value)
        {
            int bestValue    = ShadowMapResolutions.front();
            int bestDistance = std::abs(value - bestValue);
            for (int candidate : ShadowMapResolutions)
            {
                int distance = std::abs(value - candidate);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    bestValue    = candidate;
                }
            }
            return bestValue;
        }

        inline uint32_t NormalizeMSAASamples(uint32_t value)
        {
            for (uint32_t candidate : MSAASamples)
            {
                if (candidate == value)
                    return candidate;
            }
            return MSAASamples.front();
        }

        inline int NormalizeToneMappingMode(int value)
        {
            for (int candidate : ToneMappingModes)
            {
                if (candidate == value)
                    return candidate;
            }
            return ToneMappingModes.front();
        }
    } // namespace EditorRenderSettingDomains
    class SceneSerializer
    {
    public:
        SceneSerializer(const Ref<Scene>& scene);

        bool Serialize(const std::string& filepath, const EditorRenderSettings& renderSettings = {});
        bool Deserialize(const std::string& filepath, EditorRenderSettings* outRenderSettings = nullptr);

    private:
        Ref<Scene> m_Scene;
    };

} // namespace Engine
