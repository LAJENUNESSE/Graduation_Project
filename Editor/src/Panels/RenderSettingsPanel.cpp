#include "RenderSettingsPanel.h"
#include "Renderer/SceneRenderer.h"
#include "Scene/Scene.h"
#include "Core/Application.h"
#include "Core/Log.h"
#include "Core/FileDialogs.h"
#include "Asset/PathUtils.h"

#include <imgui.h>
#include <filesystem>

namespace Engine
{

    void RenderSettingsPanel::SetContext(
        SceneRenderer* sceneRenderer,
        PostProcessingSettings* postProcessingSettings,
        Ref<Framebuffer> hdrFramebuffer,
        Ref<Scene> scene,
        bool* showPhysicsColliders)
    {
        m_SceneRenderer = sceneRenderer;
        m_PostProcessingSettings = postProcessingSettings;
        m_HDRFramebuffer = hdrFramebuffer;
        m_Scene = scene;
        m_ShowPhysicsColliders = showPhysicsColliders;
    }

    void RenderSettingsPanel::OnImGuiRender()
    {
        auto& shadow = m_SceneRenderer->GetShadowSystem().GetSettings();
        ImGui::Begin("\xe6\xb8\xb2\xe6\x9f\x93\xe8\xae\xbe\xe7\xbd\xae");

        ImGui::Checkbox("\xe9\x98\xb4\xe5\xbd\xb1", &shadow.Enabled);

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
        if (ImGui::Combo("\xe9\x98\xb4\xe5\xbd\xb1\xe5\x88\x86\xe8\xbe\xa8\xe7\x8e\x87", &currentIdx, resolutionItems, 3))
        {
            m_SceneRenderer->GetShadowSystem().ResizeShadowMap(resolutionValues[currentIdx]);
        }

        ImGui::DragFloat("\xe9\x98\xb4\xe5\xbd\xb1\xe5\x81\x8f\xe7\xa7\xbb", &shadow.Bias, 0.001f, 0.0f, 0.05f, "%.4f");
        ImGui::DragFloat("\xe9\x98\xb4\xe5\xbd\xb1\xe8\x8c\x83\xe5\x9b\xb4", &shadow.OrthoSize, 0.5f, 5.0f, 100.0f, "%.1f");

        // CSM 设置
        ImGui::Checkbox("\xe7\xba\xa7\xe8\x81\x94\xe9\x98\xb4\xe5\xbd\xb1 (CSM)", &shadow.CSMEnabled);
        if (shadow.CSMEnabled)
        {
            ImGui::DragInt("\xe7\xba\xa7\xe8\x81\x94\xe6\x95\xb0\xe9\x87\x8f", &shadow.CascadeCount, 1, 1, 4);
            ImGui::DragFloat("\xe5\x88\x86\xe5\x89\xb2\xe5\x9b\xa0\xe5\xad\x90", &shadow.CascadeSplitLambda, 0.01f, 0.0f, 1.0f, "%.2f");
        }

        // SSAO 设置
        ImGui::Separator();
        ImGui::Text("SSAO");
        ImGui::Checkbox("\xe5\x90\xaf\xe7\x94\xa8 SSAO", &m_SceneRenderer->GetSSAOEnabled());
        if (m_SceneRenderer->GetSSAOEnabled())
        {
            ImGui::DragFloat("SSAO \xe5\x8d\x8a\xe5\xbe\x84", &m_SceneRenderer->GetSSAORadius(), 0.01f, 0.01f, 5.0f, "%.2f");
            ImGui::DragFloat("SSAO \xe5\x81\x8f\xe7\xa7\xbb", &m_SceneRenderer->GetSSAOBias(), 0.001f, 0.0f, 0.5f, "%.3f");
            ImGui::DragInt("SSAO \xe9\x87\x87\xe6\xa0\xb7\xe6\x95\xb0", &m_SceneRenderer->GetSSAOKernelSize(), 1, 4, 64);
            ImGui::DragFloat("SSAO \xe5\xbc\xba\xe5\xba\xa6", &m_SceneRenderer->GetSSAOIntensity(), 0.05f, 0.1f, 5.0f, "%.2f");
        }

        // IBL 调试
        ImGui::Separator();
        ImGui::Text("IBL \xe8\xb0\x83\xe8\xaf\x95");
        {
            const char* iblDebugItems[] = {
                "\xe6\xad\xa3\xe5\xb8\xb8\xe6\xb8\xb2\xe6\x9f\x93",
                "Irradiance Map", "Prefilter Map", "BRDF LUT",
                "\xe6\xb3\x95\xe7\xba\xbf\xe6\x96\xb9\xe5\x90\x91"
            };
            ImGui::Combo("IBL \xe8\xb0\x83\xe8\xaf\x95\xe6\xa8\xa1\xe5\xbc\x8f",
                         &m_SceneRenderer->GetIBLDebugMode(), iblDebugItems, 5);
        }

        ImGui::Separator();
        ImGui::Checkbox("Gamma \xe6\xa0\xa1\xe6\xad\xa3", &m_PostProcessingSettings->GammaCorrection);

        ImGui::Separator();
        ImGui::Text("\xe5\x90\x8e\xe5\xa4\x84\xe7\x90\x86");
        ImGui::Checkbox("\xe6\xb3\x9b\xe5\x85\x89 (Bloom)", &m_PostProcessingSettings->BloomEnabled);
        if (m_PostProcessingSettings->BloomEnabled)
        {
            ImGui::DragFloat("\xe6\xb3\x9b\xe5\x85\x89\xe9\x98\x88\xe5\x80\xbc", &m_PostProcessingSettings->BloomThreshold, 0.05f, 0.0f, 10.0f, "%.2f");
            ImGui::DragFloat("\xe6\xb3\x9b\xe5\x85\x89\xe5\xbc\xba\xe5\xba\xa6", &m_PostProcessingSettings->BloomStrength, 0.01f, 0.0f, 3.0f, "%.2f");
            ImGui::DragInt("\xe6\xb3\x9b\xe5\x85\x89\xe8\xbf\xad\xe4\xbb\xa3", &m_PostProcessingSettings->BloomIterations, 1, 1, 10);
        }

        const char* toneMappingItems[] = {"Reinhard", "ACES"};
        ImGui::Combo("\xe8\x89\xb2\xe8\xb0\x83\xe6\x98\xa0\xe5\xb0\x84", &m_PostProcessingSettings->ToneMappingMode, toneMappingItems, 2);

        ImGui::Separator();
        ImGui::Text("MSAA \xe6\x8a\x97\xe9\x94\xaf\xe9\xbd\xbf");
        {
            const char* msaaItems[] = {"\xe5\x85\xb3\xe9\x97\xad", "2x", "4x"};
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
        ImGui::Text("\xe5\xa4\xa9\xe7\xa9\xba\xe7\x9b\x92");

        if (m_SceneRenderer->GetSkyboxSystem().HasSkybox())
        {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "\xe5\xb7\xb2\xe5\x8a\xa0\xe8\xbd\xbd");
            if (ImGui::Button("\xe6\xb8\x85\xe9\x99\xa4\xe5\xa4\xa9\xe7\xa9\xba\xe7\x9b\x92"))
                m_SceneRenderer->GetSkyboxSystem().ClearSkybox();
        }
        else
        {
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "\xe6\x9c\xaa\xe5\x8a\xa0\xe8\xbd\xbd");
        }

        if (ImGui::Button("\xe5\x8a\xa0\xe8\xbd\xbd\xe5\xa4\xa9\xe7\xa9\xba\xe7\x9b\x92..."))
        {
            std::string dir = FileDialogs::OpenFile("*.jpg *.png *.tga", "\xe5\xa4\xa9\xe7\xa9\xba\xe7\x9b\x92\xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb9\xe4\xb8\xad\xe4\xbb\xbb\xe6\x84\x8f\xe4\xb8\x80\xe5\xbc\xa0\xe5\x9b\xbe");
            if (!dir.empty())
            {
                std::filesystem::path p(dir);
                std::filesystem::path folder = p.parent_path();
                std::string ext = p.extension().string();

                std::vector<std::string> suffixes = {"right", "left", "top", "bottom", "front", "back"};
                std::vector<std::string> faces;
                bool allFound = true;
                std::filesystem::path cwd = std::filesystem::current_path();
                for (const auto& s : suffixes)
                {
                    std::filesystem::path facePath = folder / (s + ext);
                    if (std::filesystem::exists(facePath))
                    {
                        // 存储为相对路径，确保跨平台可移植
                        auto rel = std::filesystem::relative(facePath, cwd);
                        faces.push_back(PathUtils::NormalizeSeparators(rel.string()));
                    }
                    else
                    {
                        allFound = false;
                        break;
                    }
                }

                if (allFound)
                    m_SceneRenderer->GetSkyboxSystem().LoadSkybox(faces);
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
        ImGui::Text("\xe5\xb8\xa7\xe7\x8e\x87\xe6\x8e\xa7\xe5\x88\xb6");
        {
            auto& app = Application::Get();
            bool vsync = app.GetWindow().IsVSync();
            if (ImGui::Checkbox("VSync", &vsync))
                app.GetWindow().SetVSync(vsync);

            bool fpsLimit = app.IsFrameRateLimitEnabled();
            if (ImGui::Checkbox("\xe5\xb8\xa7\xe7\x8e\x87\xe9\x99\x90\xe5\x88\xb6", &fpsLimit))
                app.SetFrameRateLimitEnabled(fpsLimit);

            float targetFps = app.GetTargetFrameRate();
            if (ImGui::DragFloat("\xe7\x9b\xae\xe6\xa0\x87 FPS", &targetFps, 1.0f, 15.0f, 300.0f, "%.0f"))
                app.SetTargetFrameRate(targetFps);
        }

        // 物理设置
        ImGui::Separator();
        ImGui::Text("\xe7\x89\xa9\xe7\x90\x86\xe8\xae\xbe\xe7\xbd\xae");
        {
            const char* backendItems[] = {"\xe6\x89\x8b\xe5\x86\x99\xe7\x89\xa9\xe7\x90\x86", "Bullet3"};
            int currentBackend = static_cast<int>(m_Scene->GetPhysicsBackend());
            if (ImGui::Combo("\xe7\x89\xa9\xe7\x90\x86\xe5\x90\x8e\xe7\xab\xaf", &currentBackend, backendItems, 2))
            {
                m_Scene->SetPhysicsBackend(static_cast<PhysicsBackend>(currentBackend));
            }
        }
        ImGui::Checkbox("\xe6\x98\xbe\xe7\xa4\xba\xe7\xa2\xb0\xe6\x92\x9e\xe4\xbd\x93", m_ShowPhysicsColliders);

        ImGui::End();
    }

} // namespace Engine
