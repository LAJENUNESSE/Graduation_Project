#include "engpch.h"
#include "Renderer/Texture.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLTexture.h"
#include "Renderer/RendererAPI.h"

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
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<Texture2D> Texture2D::Create(const void* data, uint32_t width, uint32_t height,
                                      const TextureSpecification& spec)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLTexture2D>(data, width, height, spec);
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
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
