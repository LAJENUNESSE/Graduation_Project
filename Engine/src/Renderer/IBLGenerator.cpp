#include "engpch.h"
#include "Renderer/IBLGenerator.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLIBLGenerator.h"
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
            ENGINE_CORE_WARN("[Vulkan] IBLGenerator not yet implemented, returning nullptr");
            return nullptr;
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
