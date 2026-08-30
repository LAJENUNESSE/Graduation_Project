#include "engpch.h"
#include "Debug/GPUTimerQuery.h"

#include "Core/Assert.h"
#include "Renderer/RendererAPI.h"

#include "Platform/OpenGL/OpenGLGPUTimerQuery.h"

namespace Engine
{

    Ref<GPUTimerQuery> GPUTimerQuery::Create()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLGPUTimerQuery>();
        case RendererAPI::API::Vulkan:
            // Vulkan 时间戳实现由 feat(vulkan) commit 接入；此前 Vulkan path 保持
            // 计时禁用（与改造前 m_Disabled 行为一致）
            return CreateRef<OpenGLGPUTimerQuery>();
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
