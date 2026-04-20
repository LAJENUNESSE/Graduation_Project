#pragma once

#include "Core/Base.h"
#include "Renderer/IBLGenerator.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include "Renderer/VertexArray.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Engine
{

    class SkyboxSystem
    {
    public:
        void                            Init();
        void                            Render(const glm::mat4& viewMatrix, const glm::mat4& projection);
        void                            LoadSkybox(const std::vector<std::string>& facePaths);
        void                            ClearSkybox();
        bool                            HasSkybox() const { return m_SkyboxTexture != nullptr; }
        const std::vector<std::string>& GetFacePaths() const { return m_FacePaths; }

        // IBL 资源访问
        uint32_t GetIrradianceMapID() const { return m_IBL ? m_IBL->GetIrradianceMapID() : 0; }
        uint32_t GetPrefilterMapID() const { return m_IBL ? m_IBL->GetPrefilterMapID() : 0; }
        uint32_t GetBRDFLutID() const { return m_IBL ? m_IBL->GetBRDFLutID() : 0; }
        bool     HasIBL() const { return m_IBL && m_IBL->IsReady(); }

    private:
        Ref<Shader>              m_SkyboxShader;
        Ref<VertexArray>         m_SkyboxVAO;
        Ref<TextureCubemap>      m_SkyboxTexture;
        std::vector<std::string> m_FacePaths;

        // IBL generator (platform-specific)
        Ref<IBLGenerator> m_IBL;
    };

} // namespace Engine
