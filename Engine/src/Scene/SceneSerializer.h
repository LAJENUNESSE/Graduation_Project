#pragma once

#include "Core/Base.h"
#include "Renderer/PostProcessing.h"

#include <string>

namespace Engine
{

    class Scene;

    // 编辑器级别的渲染设置（不属于场景，但需要跟场景一起保存）
    struct EditorRenderSettings
    {
        PostProcessingSettings PostProcessing;
        uint32_t MSAASamples = 1;
        int PhysicsBackend = 0; // 0=手写, 1=Bullet3
    };

    class SceneSerializer
    {
    public:
        SceneSerializer(const Ref<Scene>& scene);

        bool Serialize(const std::string& filepath, const EditorRenderSettings& renderSettings = {});
        bool Deserialize(const std::string& filepath, EditorRenderSettings* outRenderSettings = nullptr);

    private:
        Ref<Scene> m_Scene;
    };

} // namespace Engine
