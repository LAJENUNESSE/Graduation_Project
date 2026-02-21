#pragma once

#include "Renderer/GraphicsContext.h"

struct GLFWwindow;

namespace Engine
{

    class OpenGLContext : public GraphicsContext
    {
    public:
        OpenGLContext(GLFWwindow* windowHandle);

        void Init() override;
        void SwapBuffers() override;

    private:
        GLFWwindow* m_WindowHandle;
    };

} // namespace Engine
