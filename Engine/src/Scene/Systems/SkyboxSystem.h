#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/VertexArray.h"
#include "Renderer/Texture.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Engine
{

    class SkyboxSystem
    {
    public:
        void Init();
        void Render(const glm::mat4& viewMatrix, const glm::mat4& projection);
        void LoadSkybox(const std::vector<std::string>& facePaths);
        void ClearSkybox();
        bool HasSkybox() const { return m_SkyboxTexture != nullptr; }
        const std::vector<std::string>& GetFacePaths() const { return m_FacePaths; }

    private:
        Ref<Shader> m_SkyboxShader;
        Ref<VertexArray> m_SkyboxVAO;
        Ref<TextureCubemap> m_SkyboxTexture;
        std::vector<std::string> m_FacePaths;
    };

} // namespace Engine
