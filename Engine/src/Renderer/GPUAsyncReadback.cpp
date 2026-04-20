#include "engpch.h"
#include "Renderer/GPUAsyncReadback.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLAsyncReadback.h"
#include "Renderer/RendererAPI.h"

namespace Engine
{

    Ref<GPUAsyncReadback> GPUAsyncReadback::Create(uint32_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLAsyncReadback>(size);
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
