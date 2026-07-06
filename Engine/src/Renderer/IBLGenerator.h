#pragma once

#include "Core/Base.h"
#include "Renderer/Texture.h"

#include <cstdint>

namespace Engine
{

    class Shader;

    // Abstract IBL (Image-Based Lighting) resource generator.
    // Encapsulates BRDF LUT, irradiance map, and prefiltered environment map creation.
    // All GPU-specific code (compute dispatch, cubemap assembly) lives in platform implementations.
    class IBLGenerator
    {
    public:
        virtual ~IBLGenerator() = default;

        // Initialize: load compute shaders, validate, generate BRDF LUT.
        // Returns false if shader compilation fails.
        virtual bool Init() = 0;

        // Generate irradiance and prefilter maps from the given skybox cubemap.
        virtual void Generate(const Ref<TextureCubemap>& skybox) = 0;

        // Clear irradiance + prefilter maps (called when skybox is removed).
        virtual void Clear() = 0;

        // Shutdown: release all resources including BRDF LUT.
        virtual void Shutdown() = 0;

        // Access generated resource IDs (platform texture handles for binding)
        virtual uint32_t GetIrradianceMapID() const = 0;
        virtual uint32_t GetPrefilterMapID() const  = 0;
        virtual uint32_t GetBRDFLutID() const       = 0;
        virtual bool     IsReady() const            = 0;

        // Vulkan path 资源句柄（按 API 分派 — 调用方 if (Vulkan) 走 view，否则走 GetXxxMapID）。
        // 返回 void* 透传以避免 vulkan.h 泄漏到 Engine/src/（Phase 1 隔离）。
        // OpenGL 实现默认 nullptr；Vulkan 实现 reinterpret_cast<void*>(VkImageView / VkSampler)。
        virtual void* GetIrradianceView() const { return nullptr; }
        virtual void* GetPrefilterView() const { return nullptr; }
        virtual void* GetBRDFLutView() const { return nullptr; }
        virtual void* GetIBLSampler() const { return nullptr; }

        static Ref<IBLGenerator> Create();
    };

} // namespace Engine
