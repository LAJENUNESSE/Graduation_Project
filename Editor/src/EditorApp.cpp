#include "EditorLayer.h"
#include "VulkanSmokeLayer.h"
#include "Engine.h"

#include "Core/EntryPoint.h"

namespace Engine
{

    class EditorApplication : public Application
    {
    public:
        EditorApplication()
        {
            // Vulkan backend: run smoke layer until full editor rendering pipeline is ready
            if (RendererAPI::GetAPI() != RendererAPI::API::Vulkan)
                PushLayer(CreateScope<EditorLayer>());
            else
            {
                ENGINE_CORE_INFO("[Vulkan] Running smoke draw loop (EditorLayer skipped)");
                PushLayer(CreateScope<VulkanSmokeLayer>());
            }
        }

        ~EditorApplication() override = default;
    };

    Scope<Application> CreateApplication()
    {
        return CreateScope<EditorApplication>();
    }

} // namespace Engine