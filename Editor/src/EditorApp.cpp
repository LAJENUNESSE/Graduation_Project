#include "EditorLayer.h"
#include "Engine.h"

#include "Core/EntryPoint.h"

namespace Engine
{

    class EditorApplication : public Application
    {
    public:
        EditorApplication()
        {
            // Vulkan backend: skip EditorLayer until rendering pipeline is ready
            if (RendererAPI::GetAPI() != RendererAPI::API::Vulkan)
                PushLayer(CreateScope<EditorLayer>());
            else
                ENGINE_CORE_INFO("[Vulkan] Running minimal clear-color loop (EditorLayer skipped)");
        }

        ~EditorApplication() override = default;
    };

    Scope<Application> CreateApplication()
    {
        return CreateScope<EditorApplication>();
    }

} // namespace Engine