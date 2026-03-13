#include "RenderSettingsPanel.h"
#include "Asset/PathUtils.h"
#include "Core/Application.h"
#include "Core/FileDialogs.h"
#include "Core/Log.h"
#include "Renderer/SceneRenderer.h"
#include "Scene/Scene.h"

#include <filesystem>
#include <imgui.h>

namespace Engine
{

    void RenderSettingsPanel::SetContext(SceneRenderer* sceneRenderer, PostProcessingSettings* postProcessingSettings,
                                         Ref<Framebuffer> hdrFramebuffer, Ref<Scene> scene, bool* showPhysicsColliders)
    {
        m_SceneRenderer = sceneRenderer;
        m_PostProcessingSettings = postProcessingSettings;
        m_HDRFramebuffer = hdrFramebuffer;
        m_Scene = scene;
        m_ShowPhysicsColliders = showPhysicsColliders;
    }

    void RenderSettingsPanel::OnImGuiRender()
    {
        auto& shadow = m_Scene->GetShadowSettings();
        bool shadowDirty = false;
        ImGui::Begin("渲染设置");

        shadowDirty |= ImGui::Checkbox("阴影", &shadow.Enabled);

        const char* resolutionItems[] = {"512", "1024", "2048"};
        int resolutionValues[] = {512, 1024, 2048};
        int currentIdx = 1;
        for (int i = 0; i < 3; i++)
        {
            if (shadow.MapResolution == resolutionValues[i])
            {
                currentIdx = i;
                break;
            }
        }
        if (ImGui::Combo("阴影分辨率", &currentIdx, resolutionItems, 3))
            m_Scene->ResizeShadowMap(resolutionValues[currentIdx]);

        shadowDirty |= ImGui::DragFloat("阴影偏移", &shadow.Bias, 0.001f, 0.0f, 0.05f, "%.4f");
        shadowDirty |= ImGui::DragFloat("阴影范围", &shadow.OrthoSize, 0.5f, 5.0f, 100.0f, "%.1f");

        // CSM 设置
        shadowDirty |= ImGui::Checkbox("级联阴影 (CSM)", &shadow.CSMEnabled);
        if (shadow.CSMEnabled)
        {
            shadowDirty |= ImGui::DragInt("级联数量", &shadow.CascadeCount, 1, 1, 4);
            shadowDirty |= ImGui::DragFloat("分割因子", &shadow.CascadeSplitLambda, 0.01f, 0.0f, 1.0f, "%.2f");
        }

        if (shadowDirty)
            m_SceneRenderer->GetShadowSystem().GetSettings() = shadow;

        // SSAO 设置
        ImGui::Separator();
        ImGui::Text("SSAO");
        ImGui::Checkbox("启用 SSAO", &m_SceneRenderer->GetSSAOEnabled());
        if (m_SceneRenderer->GetSSAOEnabled())
        {
            ImGui::DragFloat("SSAO 半径", &m_SceneRenderer->GetSSAORadius(), 0.01f, 0.01f, 5.0f, "%.2f");
            ImGui::DragFloat("SSAO 偏移", &m_SceneRenderer->GetSSAOBias(), 0.001f, 0.0f, 0.5f, "%.3f");
            ImGui::DragInt("SSAO 采样数", &m_SceneRenderer->GetSSAOKernelSize(), 1, 4, 64);
            ImGui::DragFloat("SSAO 强度", &m_SceneRenderer->GetSSAOIntensity(), 0.05f, 0.1f, 5.0f, "%.2f");
        }

        // IBL 调试
        ImGui::Separator();
        ImGui::Text("IBL 调试");
        {
            const char* iblDebugItems[] = {"正常渲染", "Irradiance Map", "Prefilter Map", "BRDF LUT", "法线方向"};
            ImGui::Combo("IBL 调试模式", &m_SceneRenderer->GetIBLDebugMode(), iblDebugItems, 5);
        }

        ImGui::Separator();
        ImGui::Checkbox("Gamma 校正", &m_PostProcessingSettings->GammaCorrection);

        ImGui::Separator();
        ImGui::Text("后处理");
        ImGui::Checkbox("泛光 (Bloom)", &m_PostProcessingSettings->BloomEnabled);
        if (m_PostProcessingSettings->BloomEnabled)
        {
            ImGui::DragFloat("泛光阈值", &m_PostProcessingSettings->BloomThreshold, 0.05f, 0.0f, 10.0f, "%.2f");
            ImGui::DragFloat("泛光强度", &m_PostProcessingSettings->BloomStrength, 0.01f, 0.0f, 3.0f, "%.2f");
            ImGui::DragInt("泛光迭代", &m_PostProcessingSettings->BloomIterations, 1, 1, 10);
        }

        const char* toneMappingItems[] = {"Reinhard", "ACES"};
        ImGui::Combo("色调映射", &m_PostProcessingSettings->ToneMappingMode, toneMappingItems, 2);

        ImGui::Separator();
        ImGui::Text("MSAA 抗锯齿");
        {
            const char* msaaItems[] = {"关闭", "2x", "4x"};
            int msaaValues[] = {1, 2, 4};
            int currentMsaaIdx = 0;
            uint32_t currentSamples = m_HDRFramebuffer->GetSpecification().Samples;
            for (int i = 0; i < 3; i++)
            {
                if (currentSamples == static_cast<uint32_t>(msaaValues[i]))
                {
                    currentMsaaIdx = i;
                    break;
                }
            }
            if (ImGui::Combo("MSAA", &currentMsaaIdx, msaaItems, 3))
            {
                if (m_OnMSAAChanged)
                    m_OnMSAAChanged(static_cast<uint32_t>(msaaValues[currentMsaaIdx]));
            }
        }

        ImGui::Separator();
        ImGui::Text("天空盒");

        if (m_Scene->HasSkybox())
        {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "已加载");
            if (ImGui::Button("清除天空盒"))
                m_Scene->ClearSkybox();
        }
        else
        {
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "未加载");
        }

        if (ImGui::Button("加载天空盒..."))
        {
            std::string dir = FileDialogs::OpenFile(
                "*.jpg *.png *.tga", "天空盒文件夹中任意一张图");
            if (!dir.empty())
            {
                std::filesystem::path p(dir);
                std::filesystem::path folder = p.parent_path();
                std::string ext = p.extension().string();
                std::vector<std::string> suffixes = {"right", "left", "top", "bottom", "front", "back"};
                std::vector<std::string> faces;
                bool allFound = true;
                for (const auto& s : suffixes)
                {
                    std::filesystem::path facePath = folder / (s + ext);
                    if (std::filesystem::exists(facePath))
                    {
                        std::string relativeFacePath;
                        if (PathUtils::TryToProjectRelative(facePath, relativeFacePath))
                        {
                            faces.push_back(relativeFacePath);
                        }
                        else
                        {
                            ENGINE_WARN("天空盒贴图必须位于项目目录内: {}", facePath.string());
                            allFound = false;
                            break;
                        }
                    }
                    else
                    {
                        allFound = false;
                        break;
                    }
                }

                if (allFound)
                    m_Scene->LoadSkybox(faces);
                else
                    ENGINE_WARN("Skybox needs: right/left/top/bottom/front/back{}", ext);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("right/left/top/bottom/front/back + .jpg/.png");

        // 帧率控制
        ImGui::Separator();
        ImGui::Text("帧率控制");
        {
            auto& app = Application::Get();
            bool vsync = app.GetWindow().IsVSync();
            if (ImGui::Checkbox("VSync", &vsync))
                app.GetWindow().SetVSync(vsync);

            bool fpsLimit = app.IsFrameRateLimitEnabled();
            if (ImGui::Checkbox("帧率限制", &fpsLimit))
                app.SetFrameRateLimitEnabled(fpsLimit);

            float targetFps = app.GetTargetFrameRate();
            if (ImGui::DragFloat("目标 FPS", &targetFps, 1.0f, 15.0f, 300.0f, "%.0f"))
                app.SetTargetFrameRate(targetFps);
        }

        // 物理设置
        ImGui::Separator();
        ImGui::Text("物理设置");
        {
            const char* backendItems[] = {"手写物理", "Bullet3"};
            int currentBackend = static_cast<int>(m_Scene->GetPhysicsBackend());
            if (ImGui::Combo("物理后端", &currentBackend, backendItems, 2))
                m_Scene->SetPhysicsBackend(static_cast<PhysicsBackend>(currentBackend));
        }
        ImGui::Checkbox("显示碰撞体", m_ShowPhysicsColliders);

        ImGui::End();
    }

} // namespace Engine

