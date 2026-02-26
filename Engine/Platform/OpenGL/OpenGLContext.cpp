#include "engpch.h"
#include "Platform/OpenGL/OpenGLContext.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Renderer/RendererCapabilities.h"

namespace Engine
{

    OpenGLContext::OpenGLContext(GLFWwindow* windowHandle) : m_WindowHandle(windowHandle)
    {
        ENGINE_CORE_ASSERT(windowHandle, "Window handle is null!");
    }

    void OpenGLContext::Init()
    {
        glfwMakeContextCurrent(m_WindowHandle);
        int version = gladLoadGL(glfwGetProcAddress);
        ENGINE_CORE_ASSERT(version, "Failed to initialize Glad!");

        ENGINE_CORE_INFO("OpenGL Info:");
        ENGINE_CORE_INFO("  Vendor:   {0}", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
        ENGINE_CORE_INFO("  Renderer: {0}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        ENGINE_CORE_INFO("  Version:  {0}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

        RendererCapabilities::Get().Query();
    }

    void OpenGLContext::SwapBuffers()
    {
        glfwSwapBuffers(m_WindowHandle);
    }

} // namespace Engine
