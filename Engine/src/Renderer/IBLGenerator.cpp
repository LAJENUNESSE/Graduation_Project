#include "engpch.h"
#include "Renderer/IBLGenerator.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLIBLGenerator.h"
#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanIBLGenerator.h"
#endif
#include "Renderer/RendererAPI.h"

namespace Engine
{

    Ref<IBLGenerator> IBLGenerator::Create()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLIBLGenerator>();
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            return CreateRef<VulkanIBLGenerator>();
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
