#include "engpch.h"
#include "Renderer/RendererCapabilities.h"
#include "Core/Log.h"

#include <glad/gl.h>

namespace Engine
{

    void RendererCapabilities::Query()
    {
        glGetIntegerv(GL_MAJOR_VERSION, &GLMajorVersion);
        glGetIntegerv(GL_MINOR_VERSION, &GLMinorVersion);
        SupportsComputeShaders = (GLMajorVersion > 4) || (GLMajorVersion == 4 && GLMinorVersion >= 3);
        ENGINE_CORE_INFO("GL Capabilities: {}.{}, Compute={}", GLMajorVersion, GLMinorVersion,
                         SupportsComputeShaders ? "YES" : "NO");
    }

} // namespace Engine
