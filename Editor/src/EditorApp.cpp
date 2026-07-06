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
            // Phase 8: EditorLayer 主路径在 OpenGL/Vulkan 双后端下都接通。
            // Vulkan 路径下 SceneRenderer + IBL view 分派 + DrawIndexed 在
            // commit 3 实装完成；不再需要 VulkanSmokeLayer 兜底。
            PushLayer(CreateScope<EditorLayer>());
        }

        ~EditorApplication() override = default;
    };

    Scope<Application> CreateApplication()
    {
        return CreateScope<EditorApplication>();
    }

} // namespace Engine