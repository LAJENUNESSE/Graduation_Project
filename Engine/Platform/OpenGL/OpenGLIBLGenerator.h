#pragma once

#include "Renderer/IBLGenerator.h"
#include "Renderer/Shader.h"

namespace Engine
{

    class OpenGLIBLGenerator : public IBLGenerator
    {
    public:
        ~OpenGLIBLGenerator() override;

        bool Init() override;
        void Generate(const Ref<TextureCubemap>& skybox) override;
        void Clear() override;
        void Shutdown() override;

        uint32_t GetIrradianceMapID() const override { return m_IrradianceMapID; }
        uint32_t GetPrefilterMapID() const override { return m_PrefilterMapID; }
        uint32_t GetBRDFLutID() const override { return m_BRDFLutID; }
        bool     IsReady() const override { return m_IBLReady; }

    private:
        void     GenerateBRDFLut();
        uint32_t CreateEnvAtlas(const Ref<TextureCubemap>& skybox);

        Ref<Shader> m_IrradianceShader;
        Ref<Shader> m_PrefilterShader;
        Ref<Shader> m_BRDFLutShader;

        uint32_t m_IrradianceMapID = 0;
        uint32_t m_PrefilterMapID  = 0;
        uint32_t m_BRDFLutID       = 0;
        bool     m_IBLReady        = false;

        static constexpr int IRRADIANCE_SIZE      = 32;
        static constexpr int PREFILTER_SIZE       = 128;
        static constexpr int PREFILTER_MIP_LEVELS = 5;
        static constexpr int BRDF_LUT_SIZE        = 512;
    };

} // namespace Engine
