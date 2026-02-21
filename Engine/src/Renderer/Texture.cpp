#include "engpch.h"
#include "Renderer/Texture.h"

#include "Platform/OpenGL/OpenGLTexture.h"

namespace Engine
{

    Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
    {
        return CreateRef<OpenGLTexture2D>(width, height);
    }

    Ref<Texture2D> Texture2D::Create(const std::string& path)
    {
        return CreateRef<OpenGLTexture2D>(path);
    }

} // namespace Engine
