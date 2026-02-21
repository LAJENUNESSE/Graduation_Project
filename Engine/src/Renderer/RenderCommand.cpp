#include "engpch.h"
#include "Renderer/RenderCommand.h"

#include "Platform/OpenGL/OpenGLRendererAPI.h"

namespace Engine
{

    Scope<RendererAPI> RenderCommand::s_RendererAPI = CreateScope<OpenGLRendererAPI>();

} // namespace Engine
