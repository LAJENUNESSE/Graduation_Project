#include "engpch.h"
#include "Renderer/Texture.h"

#include "Core/Assert.h"
#include "Debug/GpuMemoryStats.h"
#include "Platform/OpenGL/OpenGLTexture.h"
#include "Renderer/RendererAPI.h"

#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanTexture.h"
#endif

#include <filesystem>

namespace Engine
{

    static std::string GpuMemPathFilename(const std::string& path)
    {
        try
        {
            return std::filesystem::path(path).filename().string();
        }
        catch (...)
        {
            return path;
        }
    }

    Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
    {
        Ref<Texture2D> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLTexture2D>(width, height);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanTexture2D>(width, height);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        GpuMemoryStats::Get().TrackResource(ref, GpuMemCategory::Texture, uint64_t(width) * height * 4,
                                            "Tex2D " + std::to_string(width) + "x" + std::to_string(height));
        return ref;
    }

    Ref<Texture2D> Texture2D::Create(const std::string& path)
    {
        Ref<Texture2D> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLTexture2D>(path);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanTexture2D>(path);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        // 路径加载固定展开为 RGBA8（stbi 4 通道），文件纹理含 mipmap 按 x4/3 估算
        const uint64_t uploadBytes = uint64_t(ref->GetWidth()) * ref->GetHeight() * 4;
        auto&          gmem        = GpuMemoryStats::Get();
        gmem.TrackResource(ref, GpuMemCategory::Texture, uploadBytes * 4 / 3 + uploadBytes / 3,
                           GpuMemPathFilename(path));
        gmem.AddUploaded(uploadBytes);
        return ref;
    }

    Ref<Texture2D> Texture2D::Create(const void* data, uint32_t width, uint32_t height)
    {
        Ref<Texture2D> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLTexture2D>(data, width, height);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanTexture2D>(data, width, height);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        const uint64_t bytes = uint64_t(width) * height * 4;
        auto&          gmem  = GpuMemoryStats::Get();
        gmem.TrackResource(ref, GpuMemCategory::Texture, bytes,
                           "Tex2D " + std::to_string(width) + "x" + std::to_string(height));
        gmem.AddUploaded(bytes);
        return ref;
    }

    Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height, const TextureSpecification& spec)
    {
        Ref<Texture2D> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLTexture2D>(width, height, spec);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanTexture2D>(width, height, spec);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        const uint64_t bpp  = GpuTextureFormatBPP(spec.Format);
        auto&          gmem = GpuMemoryStats::Get();
        gmem.TrackResource(ref, GpuMemCategory::Texture, uint64_t(width) * height * bpp,
                           "Tex2D " + std::to_string(width) + "x" + std::to_string(height));
        return ref;
    }

    Ref<Texture2D>
    Texture2D::Create(const void* data, uint32_t width, uint32_t height, const TextureSpecification& spec)
    {
        Ref<Texture2D> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLTexture2D>(data, width, height, spec);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanTexture2D>(data, width, height, spec);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        const uint64_t bpp   = GpuTextureFormatBPP(spec.Format);
        const uint64_t bytes = uint64_t(width) * height * bpp;
        auto&          gmem  = GpuMemoryStats::Get();
        gmem.TrackResource(ref, GpuMemCategory::Texture, bytes,
                           "Tex2D " + std::to_string(width) + "x" + std::to_string(height));
        gmem.AddUploaded(bytes);
        return ref;
    }

    Ref<TextureCubemap> TextureCubemap::Create(const std::vector<std::string>& facePaths)
    {
        Ref<TextureCubemap> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLTextureCubemap>(facePaths);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanTextureCubemap>(facePaths);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        // 6 面 RGBA8，含 mipmap 按 x4/3 估算
        const uint64_t faceBytes = uint64_t(ref->GetWidth()) * ref->GetHeight() * 4;
        auto&          gmem      = GpuMemoryStats::Get();
        gmem.TrackResource(ref, GpuMemCategory::Texture, faceBytes * 6 * 4 / 3 + faceBytes * 6 / 3,
                           "Cubemap " + (facePaths.empty() ? std::string() : GpuMemPathFilename(facePaths[0])));
        gmem.AddUploaded(faceBytes * 6);
        return ref;
    }

} // namespace Engine
