#include "engpch.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RendererCapabilities.h"
#include "Renderer/Shader.h"
#include "Renderer/VertexArray.h"

namespace Engine
{

    Renderer::SceneData Renderer::s_SceneData;

    void Renderer::Init()
    {
        RenderCommand::Init();
        RendererCapabilities::Get().Query();
    }

    void Renderer::Shutdown() {}

    void Renderer::OnWindowResize(uint32_t width, uint32_t height)
    {
        RenderCommand::SetViewport(0, 0, width, height);
    }

    void Renderer::BeginScene(const glm::mat4& viewProjection)
    {
        s_SceneData.ViewProjectionMatrix = viewProjection;
    }

    void Renderer::EndScene() {}

    void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform)
    {
        shader->Bind();
        shader->SetMat4("u_ViewProjection", s_SceneData.ViewProjectionMatrix);
        shader->SetMat4("u_Transform", transform);
        shader->SetMat3("u_NormalMatrix", glm::transpose(glm::inverse(glm::mat3(transform))));

        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray);
    }

} // namespace Engine
