#include "Engine.h"
#include "EditorLayer.h"

#include "Core/EntryPoint.h"

namespace Engine
{

    class EditorApplication : public Application
    {
    public:
        EditorApplication()
        {
            PushLayer(new EditorLayer());
        }

        ~EditorApplication() override = default;
    };

    Application* CreateApplication()
    {
        return new EditorApplication();
    }

} // namespace Engine
