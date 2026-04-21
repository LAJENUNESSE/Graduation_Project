#include "engpch.h"
#include "Renderer/Texture.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLTexture.h"
#include "Renderer/RendererAPI.h"

#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanTexture.h"
#endif

namespace Engine
{

    Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLTexture2D>(width, height);
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            return CreateRef<VulkanTexture2D>(width, height);
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<Texture2D> Texture2D::Create(const std::string& path)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLTexture2D>(path);
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            return CreateRef<VulkanTexture2D>(path);
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<Texture2D> Texture2D::Create(const void* data, uint32_t width, uint32_t height)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLTexture2D>(data, width, height);
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            return CreateRef<VulkanTexture2D>(data, width, height);
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height, const TextureSpecification& spec)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLTexture2D>(width, height, spec);
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            return CreateRef<VulkanTexture2D>(width, height, spec);
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<Texture2D>
    Texture2D::Create(const void* data, uint32_t width, uint32_t height, const TextureSpecification& spec)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLTexture2D>(data, width, height, spec);
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            return CreateRef<VulkanTexture2D>(data, width, height, spec);
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<TextureCubemap> TextureCubemap::Create(const std::vector<std::string>& facePaths)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLTextureCubemap>(facePaths);
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            return CreateRef<VulkanTextureCubemap>(facePaths);
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
