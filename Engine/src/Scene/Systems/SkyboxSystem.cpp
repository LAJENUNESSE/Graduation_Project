#include "engpch.h"
#include "Scene/Systems/SkyboxSystem.h"
#include "Core/Log.h"
#include "Renderer/Buffer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RendererCapabilities.h"

#include <glad/gl.h>

namespace Engine
{

    void SkyboxSystem::Init()
    {
        m_SkyboxShader = Shader::Create("assets/shaders/Skybox.glsl");

        // Skybox cube VAO (36 vertices, positions only)
        float skyboxVertices[] = {
            -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,
            -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f,
            1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,
            1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f,
            1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f,
            -1.0f, 1.0f,  -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
            -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f,
            -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,
        };

        auto skyboxVB = VertexBuffer::Create(skyboxVertices, sizeof(skyboxVertices));
        skyboxVB->SetLayout({{ShaderDataType::Float3, "a_Position"}});

        m_SkyboxVAO = VertexArray::Create();
        m_SkyboxVAO->AddVertexBuffer(skyboxVB);

        // 预计算 BRDF LUT（只需一次，与 skybox 无关）
        if (RendererCapabilities::Get().SupportsComputeShaders)
        {
            m_BRDFLutShader    = Shader::Create("assets/shaders/IBL_BRDF_LUT.glsl");
            m_IrradianceShader = Shader::Create("assets/shaders/IBL_Irradiance.glsl");
            m_PrefilterShader  = Shader::Create("assets/shaders/IBL_Prefilter.glsl");

            // ★ 验证 IBL shader 编译是否成功（RelWithDebInfo 下 assert 不终止，需运行时检查）
            auto validateShader = [](const Ref<Shader>& shader, const char* name)
            {
                if (!shader)
                {
                    ENGINE_CORE_ERROR("IBL: {} shader is null!", name);
                    return false;
                }
                // Bind 后检查 GL 错误，如果 m_RendererID==0 则 glUseProgram(0) 会导致后续 uniform 设置无效
                shader->Bind();
                GLenum err = glGetError();
                if (err != GL_NO_ERROR)
                {
                    ENGINE_CORE_ERROR("IBL: {} shader bind failed (GL error: 0x{:04X})", name, err);
                    return false;
                }
                return true;
            };

            bool allShadersValid = true;
            allShadersValid &= validateShader(m_BRDFLutShader, "BRDF_LUT");
            allShadersValid &= validateShader(m_IrradianceShader, "Irradiance");
            allShadersValid &= validateShader(m_PrefilterShader, "Prefilter");

            if (allShadersValid)
            {
                ENGINE_CORE_INFO("IBL: All compute shaders compiled successfully");
                GenerateBRDFLut();
            }
            else
            {
                ENGINE_CORE_ERROR("IBL: Shader compilation failed, IBL disabled");
                m_BRDFLutShader.reset();
                m_IrradianceShader.reset();
                m_PrefilterShader.reset();
            }
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
        m_FacePaths     = facePaths;
        m_SkyboxTexture = TextureCubemap::Create(facePaths);

        // 加载天空盒后生成 IBL 资源（需要 shader 有效）
        if (RendererCapabilities::Get().SupportsComputeShaders && m_IrradianceShader && m_PrefilterShader &&
            m_BRDFLutShader)
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

        // 创建 BRDF LUT 纹理 (RG16F) — 使用 immutable storage
        glGenTextures(1, &m_BRDFLutID);
        glBindTexture(GL_TEXTURE_2D, m_BRDFLutID);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RG16F, BRDF_LUT_SIZE, BRDF_LUT_SIZE);
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

    uint32_t SkyboxSystem::CreateEnvAtlas()
    {
        // 将 cubemap 6 面读到 CPU，创建横向排列的 2D atlas 纹理
        // 用 sampler2D 采样 atlas 替代 samplerCube，绕开 compute shader 中
        // texture(samplerCube) 在某些驱动上返回全零的问题
        uint32_t envMapID = m_SkyboxTexture->GetRendererID();
        int      faceSize = static_cast<int>(m_SkyboxTexture->GetWidth());

        std::vector<uint8_t> atlasPixels(faceSize * 6 * faceSize * 4, 0);

        glBindTexture(GL_TEXTURE_CUBE_MAP, envMapID);
        for (int face = 0; face < 6; face++)
        {
            std::vector<uint8_t> facePixels(faceSize * faceSize * 4);
            glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, GL_UNSIGNED_BYTE, facePixels.data());

            for (int y = 0; y < faceSize; y++)
            {
                int srcStart = y * faceSize * 4;
                int dstStart = (y * faceSize * 6 + face * faceSize) * 4;
                std::memcpy(&atlasPixels[dstStart], &facePixels[srcStart], faceSize * 4);
            }
        }
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

        // 创建 atlas — 使用 immutable storage（imageLoad 要求）
        uint32_t atlasID;
        glGenTextures(1, &atlasID);
        glBindTexture(GL_TEXTURE_2D, atlasID);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, faceSize * 6, faceSize);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, faceSize * 6, faceSize, GL_RGBA, GL_UNSIGNED_BYTE, atlasPixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindTexture(GL_TEXTURE_2D, 0);

        return atlasID;
    }

    void SkyboxSystem::GenerateIBL()
    {
        if (!m_SkyboxTexture)
            return;

        ENGINE_CORE_INFO("IBL: Generating IBL resources...");

        // ★ 解绑当前 FBO，compute shader 的 imageStore 在有 FBO 绑定时可能无法写入
        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 清空纹理单元残留
        for (int unit = 0; unit < 4; unit++)
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }

        // ★ 创建 2D atlas 替代 samplerCube（绕开 compute shader 中 cubemap 采样返回零的问题）
        uint32_t envAtlas = CreateEnvAtlas();

        // ---- 1. Irradiance Map ----
        {
            if (m_IrradianceMapID)
                glDeleteTextures(1, &m_IrradianceMapID);

            glGenTextures(1, &m_IrradianceMapID);
            glBindTexture(GL_TEXTURE_CUBE_MAP, m_IrradianceMapID);
            for (int i = 0; i < 6; i++)
            {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA16F, IRRADIANCE_SIZE, IRRADIANCE_SIZE, 0,
                             GL_RGBA, GL_FLOAT, nullptr);
            }
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

            // 临时 2D 输出纹理（6 面横向排列）— immutable storage
            uint32_t irradTemp;
            glGenTextures(1, &irradTemp);
            glBindTexture(GL_TEXTURE_2D, irradTemp);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, IRRADIANCE_SIZE * 6, IRRADIANCE_SIZE);
            glBindTexture(GL_TEXTURE_2D, 0);

            m_IrradianceShader->Bind();

            int envFaceSize = static_cast<int>(m_SkyboxTexture->GetWidth());
            m_IrradianceShader->SetInt("u_FaceSize", IRRADIANCE_SIZE);
            m_IrradianceShader->SetInt("u_EnvFaceSize", envFaceSize);

            // ★ binding=0 输出，binding=1 atlas 输入（用 imageLoad 替代 texture）
            glBindImageTexture(0, irradTemp, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            glBindImageTexture(1, envAtlas, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);

            uint32_t groupsX = (IRRADIANCE_SIZE * 6 + 15) / 16;
            uint32_t groupsY = (IRRADIANCE_SIZE + 15) / 16;
            RenderCommand::DispatchCompute(groupsX, groupsY, 1);
            glMemoryBarrier(GL_ALL_BARRIER_BITS);
            glFinish();

            // 读回并写入 cubemap
            std::vector<float> pixels(IRRADIANCE_SIZE * 6 * IRRADIANCE_SIZE * 4);
            glBindTexture(GL_TEXTURE_2D, irradTemp);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            glBindTexture(GL_TEXTURE_CUBE_MAP, m_IrradianceMapID);
            for (int face = 0; face < 6; face++)
            {
                std::vector<float> facePixels(IRRADIANCE_SIZE * IRRADIANCE_SIZE * 4);
                for (int y = 0; y < IRRADIANCE_SIZE; y++)
                {
                    int srcRowStart = (y * IRRADIANCE_SIZE * 6 + face * IRRADIANCE_SIZE) * 4;
                    int dstRowStart = y * IRRADIANCE_SIZE * 4;
                    std::memcpy(&facePixels[dstRowStart], &pixels[srcRowStart], IRRADIANCE_SIZE * 4 * sizeof(float));
                }
                glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, IRRADIANCE_SIZE, IRRADIANCE_SIZE,
                                GL_RGBA, GL_FLOAT, facePixels.data());
            }
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

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
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA16F, PREFILTER_SIZE, PREFILTER_SIZE, 0,
                             GL_RGBA, GL_FLOAT, nullptr);
            }
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

            for (int mip = 0; mip < PREFILTER_MIP_LEVELS; mip++)
            {
                int mipSize = PREFILTER_SIZE >> mip;
                if (mipSize < 1)
                    mipSize = 1;

                float roughness = static_cast<float>(mip) / static_cast<float>(PREFILTER_MIP_LEVELS - 1);
                roughness       = std::max(roughness, 0.05f);

                // immutable storage
                uint32_t prefilterTemp;
                glGenTextures(1, &prefilterTemp);
                glBindTexture(GL_TEXTURE_2D, prefilterTemp);
                glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, mipSize * 6, mipSize);
                glBindTexture(GL_TEXTURE_2D, 0);

                m_PrefilterShader->Bind();

                int envFaceSize = static_cast<int>(m_SkyboxTexture->GetWidth());
                m_PrefilterShader->SetInt("u_FaceSize", mipSize);
                m_PrefilterShader->SetInt("u_EnvFaceSize", envFaceSize);
                m_PrefilterShader->SetFloat("u_Roughness", roughness);

                // ★ binding=0 输出，binding=1 atlas 输入（用 imageLoad 替代 texture）
                glBindImageTexture(0, prefilterTemp, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
                glBindImageTexture(1, envAtlas, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);

                uint32_t groupsX = (mipSize * 6 + 15) / 16;
                uint32_t groupsY = (mipSize + 15) / 16;
                RenderCommand::DispatchCompute(groupsX, groupsY, 1);
                glMemoryBarrier(GL_ALL_BARRIER_BITS);
                glFinish();

                // 读回像素
                std::vector<float> pixels(mipSize * 6 * mipSize * 4);
                glBindTexture(GL_TEXTURE_2D, prefilterTemp);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
                glBindTexture(GL_TEXTURE_2D, 0);

                // NaN/Inf 清理
                for (size_t pi = 0; pi < pixels.size(); pi++)
                {
                    if (std::isnan(pixels[pi]) || std::isinf(pixels[pi]))
                        pixels[pi] = 0.0f;
                }

                glBindTexture(GL_TEXTURE_CUBE_MAP, m_PrefilterMapID);
                for (int face = 0; face < 6; face++)
                {
                    std::vector<float> facePixels(mipSize * mipSize * 4);
                    for (int y = 0; y < mipSize; y++)
                    {
                        int srcRowStart = (y * mipSize * 6 + face * mipSize) * 4;
                        int dstRowStart = y * mipSize * 4;
                        std::memcpy(&facePixels[dstRowStart], &pixels[srcRowStart], mipSize * 4 * sizeof(float));
                    }
                    glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, 0, 0, mipSize, mipSize, GL_RGBA,
                                    GL_FLOAT, facePixels.data());
                }
                glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

                glDeleteTextures(1, &prefilterTemp);
            }

            ENGINE_CORE_INFO("IBL: Prefiltered env map generated ({}x{}, {} mips)", PREFILTER_SIZE, PREFILTER_SIZE,
                             PREFILTER_MIP_LEVELS);
        }

        // 清理 atlas
        glDeleteTextures(1, &envAtlas);

        // 恢复状态
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);

        m_IBLReady = true;
        ENGINE_CORE_INFO("IBL: All resources ready");
    }

} // namespace Engine
