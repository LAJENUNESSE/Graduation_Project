#include "engpch.h"
#include "Scene/Systems/SkyboxSystem.h"
#include "Renderer/Buffer.h"
#include "Renderer/RenderCommand.h"
#include "Core/Log.h"

namespace Engine
{

    void SkyboxSystem::Init()
    {
        m_SkyboxShader = Shader::Create("assets/shaders/Skybox.glsl");

        // Skybox cube VAO (36 vertices, positions only)
        float skyboxVertices[] = {
            -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f,
        };

        auto skyboxVB = VertexBuffer::Create(skyboxVertices, sizeof(skyboxVertices));
        skyboxVB->SetLayout({{ShaderDataType::Float3, "a_Position"}});

        m_SkyboxVAO = VertexArray::Create();
        m_SkyboxVAO->AddVertexBuffer(skyboxVB);
    }

    void SkyboxSystem::Render(const glm::mat4& viewMatrix, const glm::mat4& projection)
    {
        if (!m_SkyboxTexture)
            return;

        RenderCommand::SetDepthFunc(DepthFunc::LEqual);

        m_SkyboxShader->Bind();
        glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(viewMatrix));
        m_SkyboxShader->SetMat4("u_ViewProjection", projection * viewNoTranslation);
        m_SkyboxShader->SetInt("u_Skybox", 0);
        m_SkyboxTexture->Bind(0);

        m_SkyboxVAO->Bind();
        RenderCommand::DrawArrays(36);

        RenderCommand::SetDepthFunc(DepthFunc::Less);
    }

    void SkyboxSystem::LoadSkybox(const std::vector<std::string>& facePaths)
    {
        if (facePaths.size() != 6)
        {
            ENGINE_CORE_ERROR("Skybox requires exactly 6 face paths");
            return;
        }
        m_FacePaths = facePaths;
        m_SkyboxTexture = TextureCubemap::Create(facePaths);
    }

    void SkyboxSystem::ClearSkybox()
    {
        m_SkyboxTexture.reset();
        m_FacePaths.clear();
    }

} // namespace Engine
