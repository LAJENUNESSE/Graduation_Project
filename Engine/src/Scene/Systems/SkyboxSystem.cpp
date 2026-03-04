#include "engpch.h"
#include "Scene/Systems/SkyboxSystem.h"
#include "Renderer/Buffer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RendererCapabilities.h"
#include "Core/Log.h"

#include <glad/gl.h>

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

        // 预计算 BRDF LUT（只需一次，与 skybox 无关）
        if (RendererCapabilities::Get().SupportsComputeShaders)
        {
            m_BRDFLutShader = Shader::Create("assets/shaders/IBL_BRDF_LUT.glsl");
            m_IrradianceShader = Shader::Create("assets/shaders/IBL_Irradiance.glsl");
            m_PrefilterShader = Shader::Create("assets/shaders/IBL_Prefilter.glsl");
            GenerateBRDFLut();
        }
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

        // 加载天空盒后生成 IBL 资源
        if (RendererCapabilities::Get().SupportsComputeShaders)
            GenerateIBL();
    }

    void SkyboxSystem::ClearSkybox()
    {
        m_SkyboxTexture.reset();
        m_FacePaths.clear();

        // 清理 IBL 资源
        if (m_IrradianceMapID)
        {
            glDeleteTextures(1, &m_IrradianceMapID);
            m_IrradianceMapID = 0;
        }
        if (m_PrefilterMapID)
        {
            glDeleteTextures(1, &m_PrefilterMapID);
            m_PrefilterMapID = 0;
        }
        m_IBLReady = false;
    }

    void SkyboxSystem::GenerateBRDFLut()
    {
        if (m_BRDFLutID)
            glDeleteTextures(1, &m_BRDFLutID);

        // 创建 BRDF LUT 纹理 (RG16F)
        glGenTextures(1, &m_BRDFLutID);
        glBindTexture(GL_TEXTURE_2D, m_BRDFLutID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, BRDF_LUT_SIZE, BRDF_LUT_SIZE,
                     0, GL_RG, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // 用 compute shader 填充
        m_BRDFLutShader->Bind();
        glBindImageTexture(0, m_BRDFLutID, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);

        uint32_t groupsX = (BRDF_LUT_SIZE + 15) / 16;
        uint32_t groupsY = (BRDF_LUT_SIZE + 15) / 16;
        RenderCommand::DispatchCompute(groupsX, groupsY, 1);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage | 0x00000020); // GL_TEXTURE_FETCH_BARRIER_BIT

        ENGINE_CORE_INFO("IBL: BRDF LUT generated ({}x{})", BRDF_LUT_SIZE, BRDF_LUT_SIZE);
    }

    void SkyboxSystem::GenerateIBL()
    {
        if (!m_SkyboxTexture)
            return;

        uint32_t envMapID = m_SkyboxTexture->GetRendererID();

        // ---- 1. Irradiance Map ----
        {
            if (m_IrradianceMapID)
                glDeleteTextures(1, &m_IrradianceMapID);

            // 创建 cubemap 纹理
            glGenTextures(1, &m_IrradianceMapID);
            glBindTexture(GL_TEXTURE_CUBE_MAP, m_IrradianceMapID);
            for (int i = 0; i < 6; i++)
            {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA16F,
                             IRRADIANCE_SIZE, IRRADIANCE_SIZE, 0, GL_RGBA, GL_FLOAT, nullptr);
            }
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

            // 使用一张 2D 临时纹理作为 compute shader 输出（6 面横向排列）
            uint32_t irradTemp;
            glGenTextures(1, &irradTemp);
            glBindTexture(GL_TEXTURE_2D, irradTemp);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                         IRRADIANCE_SIZE * 6, IRRADIANCE_SIZE, 0, GL_RGBA, GL_FLOAT, nullptr);

            m_IrradianceShader->Bind();
            // 绑定环境 cubemap
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, envMapID);
            m_IrradianceShader->SetInt("u_EnvironmentMap", 0);
            m_IrradianceShader->SetInt("u_FaceSize", IRRADIANCE_SIZE);

            glBindImageTexture(0, irradTemp, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

            uint32_t groupsX = (IRRADIANCE_SIZE * 6 + 15) / 16;
            uint32_t groupsY = (IRRADIANCE_SIZE + 15) / 16;
            RenderCommand::DispatchCompute(groupsX, groupsY, 1);
            RenderCommand::MemoryBarrier(BarrierBit::All);

            // 从临时 2D 纹理读回并写入 cubemap 各面
            std::vector<float> pixels(IRRADIANCE_SIZE * 6 * IRRADIANCE_SIZE * 4);
            glBindTexture(GL_TEXTURE_2D, irradTemp);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());

            glBindTexture(GL_TEXTURE_CUBE_MAP, m_IrradianceMapID);
            for (int face = 0; face < 6; face++)
            {
                // 从横向排列中提取每个面的像素
                std::vector<float> facePixels(IRRADIANCE_SIZE * IRRADIANCE_SIZE * 4);
                for (int y = 0; y < IRRADIANCE_SIZE; y++)
                {
                    int srcRowStart = (y * IRRADIANCE_SIZE * 6 + face * IRRADIANCE_SIZE) * 4;
                    int dstRowStart = y * IRRADIANCE_SIZE * 4;
                    std::memcpy(&facePixels[dstRowStart], &pixels[srcRowStart],
                                IRRADIANCE_SIZE * 4 * sizeof(float));
                }
                glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0,
                                IRRADIANCE_SIZE, IRRADIANCE_SIZE,
                                GL_RGBA, GL_FLOAT, facePixels.data());
            }

            glDeleteTextures(1, &irradTemp);
            ENGINE_CORE_INFO("IBL: Irradiance map generated ({}x{})", IRRADIANCE_SIZE, IRRADIANCE_SIZE);
        }

        // ---- 2. Prefiltered Environment Map ----
        {
            if (m_PrefilterMapID)
                glDeleteTextures(1, &m_PrefilterMapID);

            glGenTextures(1, &m_PrefilterMapID);
            glBindTexture(GL_TEXTURE_CUBE_MAP, m_PrefilterMapID);
            for (int i = 0; i < 6; i++)
            {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA16F,
                             PREFILTER_SIZE, PREFILTER_SIZE, 0, GL_RGBA, GL_FLOAT, nullptr);
            }
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

            m_PrefilterShader->Bind();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, envMapID);
            m_PrefilterShader->SetInt("u_EnvironmentMap", 0);

            for (int mip = 0; mip < PREFILTER_MIP_LEVELS; mip++)
            {
                int mipSize = PREFILTER_SIZE >> mip;
                if (mipSize < 1) mipSize = 1;

                float roughness = static_cast<float>(mip) / static_cast<float>(PREFILTER_MIP_LEVELS - 1);

                // 创建临时 2D 纹理
                uint32_t prefilterTemp;
                glGenTextures(1, &prefilterTemp);
                glBindTexture(GL_TEXTURE_2D, prefilterTemp);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                             mipSize * 6, mipSize, 0, GL_RGBA, GL_FLOAT, nullptr);

                m_PrefilterShader->Bind();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_CUBE_MAP, envMapID);
                m_PrefilterShader->SetInt("u_EnvironmentMap", 0);
                m_PrefilterShader->SetInt("u_FaceSize", mipSize);
                m_PrefilterShader->SetFloat("u_Roughness", roughness);

                glBindImageTexture(0, prefilterTemp, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

                uint32_t groupsX = (mipSize * 6 + 15) / 16;
                uint32_t groupsY = (mipSize + 15) / 16;
                RenderCommand::DispatchCompute(groupsX, groupsY, 1);
                RenderCommand::MemoryBarrier(BarrierBit::All);

                // 读回并写入 cubemap 对应 mip
                std::vector<float> pixels(mipSize * 6 * mipSize * 4);
                glBindTexture(GL_TEXTURE_2D, prefilterTemp);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());

                glBindTexture(GL_TEXTURE_CUBE_MAP, m_PrefilterMapID);
                for (int face = 0; face < 6; face++)
                {
                    std::vector<float> facePixels(mipSize * mipSize * 4);
                    for (int y = 0; y < mipSize; y++)
                    {
                        int srcRowStart = (y * mipSize * 6 + face * mipSize) * 4;
                        int dstRowStart = y * mipSize * 4;
                        std::memcpy(&facePixels[dstRowStart], &pixels[srcRowStart],
                                    mipSize * 4 * sizeof(float));
                    }
                    glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, 0, 0,
                                    mipSize, mipSize, GL_RGBA, GL_FLOAT, facePixels.data());
                }

                glDeleteTextures(1, &prefilterTemp);
            }

            ENGINE_CORE_INFO("IBL: Prefiltered env map generated ({}x{}, {} mips)",
                             PREFILTER_SIZE, PREFILTER_SIZE, PREFILTER_MIP_LEVELS);
        }

        m_IBLReady = true;
        ENGINE_CORE_INFO("IBL: All resources ready");
    }

} // namespace Engine
