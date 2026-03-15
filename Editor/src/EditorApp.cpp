#include "EditorLayer.h"
#include "Engine.h"

#include "Core/EntryPoint.h"

namespace Engine
{

    class EditorApplication : public Application
    {
    public:
        EditorApplication() { PushLayer(CreateScope<EditorLayer>()); }

        ~EditorApplication() override = default;
    };

    Scope<Application> CreateApplication()
    {
        return CreateScope<EditorApplication>();
    }

} // namespace Engine