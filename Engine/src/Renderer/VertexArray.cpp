#include "engpch.h"
#include "Renderer/VertexArray.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLVertexArray.h"
#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanVertexArray.h"
#endif
#include "Renderer/RendererAPI.h"

namespace Engine
{

    Ref<VertexArray> VertexArray::Create()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLVertexArray>();
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            return CreateRef<VulkanVertexArray>();
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
