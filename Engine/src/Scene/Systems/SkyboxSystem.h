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

        // IBL 资源访问
        uint32_t GetIrradianceMapID() const { return m_IrradianceMapID; }
        uint32_t GetPrefilterMapID() const { return m_PrefilterMapID; }
        uint32_t GetBRDFLutID() const { return m_BRDFLutID; }
        bool HasIBL() const { return m_IBLReady; }

    private:
        void GenerateIBL();
        void GenerateBRDFLut();
        uint32_t CreateEnvAtlas(); // 将 cubemap 6 面读到 CPU，创建 2D atlas 纹理

        Ref<Shader> m_SkyboxShader;
        Ref<VertexArray> m_SkyboxVAO;
        Ref<TextureCubemap> m_SkyboxTexture;
        std::vector<std::string> m_FacePaths;

        // IBL 资源
        Ref<Shader> m_IrradianceShader;
        Ref<Shader> m_PrefilterShader;
        Ref<Shader> m_BRDFLutShader;

        uint32_t m_IrradianceMapID = 0;   // GL cubemap texture ID
        uint32_t m_PrefilterMapID = 0;     // GL cubemap texture ID
        uint32_t m_BRDFLutID = 0;          // GL 2D texture ID
        bool m_IBLReady = false;

        static constexpr int IRRADIANCE_SIZE = 32;
        static constexpr int PREFILTER_SIZE = 128;
        static constexpr int PREFILTER_MIP_LEVELS = 5;
        static constexpr int BRDF_LUT_SIZE = 512;
    };

} // namespace Engine
