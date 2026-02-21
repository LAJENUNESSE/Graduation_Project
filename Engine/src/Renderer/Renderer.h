#pragma once

#include "Core/Base.h"

#include <glm/glm.hpp>

namespace Engine
{

    class Shader;
    class VertexArray;

    class Renderer
    {
    public:
        static void Init();
        static void Shutdown();
        static void OnWindowResize(uint32_t width, uint32_t height);

        static void BeginScene(const glm::mat4& viewProjection);
        static void EndScene();

        static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray,
                           const glm::mat4& transform = glm::mat4(1.0f));

    private:
        struct SceneData
        {
            glm::mat4 ViewProjectionMatrix;
        };

        static SceneData s_SceneData;
    };

} // namespace Engine
