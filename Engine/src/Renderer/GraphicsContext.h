#pragma once

#include "Core/Base.h"

namespace Engine
{

    class GraphicsContext
    {
    public:
        virtual ~GraphicsContext() = default;

        virtual void Init()        = 0;
        virtual void SwapBuffers() = 0;

        // 高级帧录制接口：让主循环（Application::Run）显式控制帧边界。
        // - OpenGL: 默认 no-op；present 仍由 SwapBuffers 完成
        // - Vulkan: BeginRenderFrame -> BeginFrame (acquire swapchain + Begin cmd)
        //           EndRenderFrame   -> RecordDefaultPasses + EndFrame (submit + present)
        //           帧已在 EndRenderFrame 内完成，后续 SwapBuffers 退化为 no-op
        //
        // 主循环模板：
        //   ctx->BeginRenderFrame();
        //   ... layers->OnUpdate (粒子/流体 dispatch 录主帧 cmd) + ImGui ...
        //   ctx->EndRenderFrame();
        //   window->OnUpdate();   // glfwPollEvents (+ OpenGL: SwapBuffers)
        virtual void BeginRenderFrame() {}
        virtual void EndRenderFrame() {}

        static Scope<GraphicsContext> Create(void* window);
    };

} // namespace Engine
