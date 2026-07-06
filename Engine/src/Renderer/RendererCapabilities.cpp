#include "engpch.h"
#include "Renderer/RendererCapabilities.h"
#include "Core/Log.h"
#include "Renderer/RenderCommand.h"

namespace Engine
{

    void RendererCapabilities::Query()
    {
        RenderCommand::QueryCapabilities(*this);
        ENGINE_CORE_INFO("Renderer Capabilities: {}.{}, Compute={}, Vendor={}, Renderer={}",
                         MajorVersion, MinorVersion,
                         SupportsComputeShaders ? "YES" : "NO",
                         VendorString, RendererString);
    }

} // namespace Engine
