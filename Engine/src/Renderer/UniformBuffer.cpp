#include "engpch.h"
#include "Renderer/UniformBuffer.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLUniformBuffer.h"
#include "Renderer/RendererAPI.h"

namespace Engine
{

    Ref<UniformBuffer> UniformBuffer::Create(uint32_t size, uint32_t binding)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLUniformBuffer>(size, binding);
        case RendererAPI::API::Vulkan:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not yet implemented!");
            return nullptr;
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
