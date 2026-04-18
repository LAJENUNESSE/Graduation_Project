#include "RenderSettingsPanel.h"
#include "Asset/PathUtils.h"
#include "Core/Application.h"
#include "Core/FileDialogs.h"
#include "Core/Log.h"
#include "Renderer/ParticleSystemGPU.h"
#include "Renderer/SceneRenderer.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <filesystem>
#include <imgui.h>

namespace Engine
{
    namespace
    {
        const char* ABSourceLabel(ParticleSystemGPU::ABConfigSource source)
        {
            return ParticleSystemGPU::ABConfigSourceLabel(source);
        }

        template <typename T, size_t N> int FindSelectedIndex(const std::array<T, N>& values, const T& currentValue)
        {
            for (size_t i = 0; i < values.size(); ++i)
            {
                if (values[i] == currentValue)
                    return static_cast<int>(i);
            }
            return 0;
        }
    } // namespace
    void RenderSettingsPanel::SetContext(SceneRenderer*          sceneRenderer,
                                         PostProcessingSettings* postProcessingSettings,
                                         Ref<Framebuffer>        hdrFramebuffer,
                                         Ref<Scene>              scene,
                                         bool*                   showPhysicsColliders,
                                         bool*                   showMeshSDFBounds)
    {
        m_SceneRenderer          = sceneRenderer;
        m_PostProcessingSettings = postProcessingSettings;
        m_HDRFramebuffer         = hdrFramebuffer;
        m_Scene                  = scene;
        m_ShowPhysicsColliders   = showPhysicsColliders;
        m_ShowMeshSDFBounds      = showMeshSDFBounds;
    }

    void RenderSettingsPanel::OnImGuiRender()
    {
        ImGui::Begin("渲染设置");

        if (!m_Scene || !m_SceneRenderer || !m_PostProcessingSettings || !m_HDRFramebuffer || !m_ShowPhysicsColliders ||
            !m_ShowMeshSDFBounds)
        {
            ImGui::TextDisabled("渲染设置上下文未就绪");
            ImGui::End();
            return;
        }

        auto& shadow      = m_Scene->GetShadowSettings();
        bool  shadowDirty = false;

        if (m_ReadOnly)
            ImGui::BeginDisabled();

        shadowDirty |= ImGui::Checkbox("阴影", &shadow.Enabled);

        int currentIdx = FindSelectedIndex(EditorRenderSettingDomains::ShadowMapResolutions, shadow.MapResolution);
        if (ImGui::Combo("阴影分辨率", &currentIdx, EditorRenderSettingDomains::ShadowMapResolutionLabels.data(),
                         static_cast<int>(EditorRenderSettingDomains::ShadowMapResolutionLabels.size())))
        {
            m_Scene->ResizeShadowMap(EditorRenderSettingDomains::ShadowMapResolutions[static_cast<size_t>(currentIdx)]);
        }

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

        int toneMappingMode =
            EditorRenderSettingDomains::NormalizeToneMappingMode(m_PostProcessingSettings->ToneMappingMode);
        int currentToneMappingIdx = FindSelectedIndex(EditorRenderSettingDomains::ToneMappingModes, toneMappingMode);
        if (ImGui::Combo("色调映射", &currentToneMappingIdx, EditorRenderSettingDomains::ToneMappingModeLabels.data(),
                         static_cast<int>(EditorRenderSettingDomains::ToneMappingModeLabels.size())))
        {
            m_PostProcessingSettings->ToneMappingMode =
                EditorRenderSettingDomains::ToneMappingModes[static_cast<size_t>(currentToneMappingIdx)];
        }

        ImGui::Separator();
        ImGui::Text("MSAA 抗锯齿");
        {
            uint32_t currentSamples = m_HDRFramebuffer->GetSpecification().Samples;
            int      currentMsaaIdx = FindSelectedIndex(EditorRenderSettingDomains::MSAASamples, currentSamples);
            if (ImGui::Combo("MSAA", &currentMsaaIdx, EditorRenderSettingDomains::MSAASampleLabels.data(),
                             static_cast<int>(EditorRenderSettingDomains::MSAASampleLabels.size())))
            {
                if (m_OnMSAAChanged)
                    m_OnMSAAChanged(EditorRenderSettingDomains::MSAASamples[static_cast<size_t>(currentMsaaIdx)]);
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
            std::string dir = FileDialogs::OpenFile("*.jpg *.png *.tga", "天空盒文件夹中任意一张图");
            if (!dir.empty())
            {
                std::filesystem::path    p(dir);
                std::filesystem::path    folder   = p.parent_path();
                std::string              ext      = p.extension().string();
                std::vector<std::string> suffixes = {"right", "left", "top", "bottom", "front", "back"};
                std::vector<std::string> faces;
                bool                     allFound = true;
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
            auto& app   = Application::Get();
            bool  vsync = app.GetWindow().IsVSync();
            if (ImGui::Checkbox("VSync", &vsync))
                app.GetWindow().SetVSync(vsync);

            bool fpsLimit = app.IsFrameRateLimitEnabled();
            if (ImGui::Checkbox("帧率限制", &fpsLimit))
                app.SetFrameRateLimitEnabled(fpsLimit);

            float targetFps = app.GetTargetFrameRate();
            if (ImGui::DragFloat("目标 FPS", &targetFps, 1.0f, 15.0f, 300.0f, "%.0f"))
                app.SetTargetFrameRate(targetFps);
        }

        // 粒子 AB 诊断开关（仅用于定位）
        ImGui::Separator();
        ImGui::Text("粒子 AB 诊断");
        {
            auto ab = ParticleSystemGPU::GetABConfigSnapshot();

            bool forceGL          = ab.ForceGL;
            bool disableReadback  = ab.DisableCounterReadback;
            bool changed          = false;

            if (ab.ForceGLLockedByEnv)
            {
                ImGui::BeginDisabled();
                ImGui::Checkbox("强制GL Compute", &forceGL);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("ENV锁定");
            }
            else if (ImGui::Checkbox("强制GL Compute", &forceGL))
            {
                changed = true;
            }

            if (ab.DisableReadbackLockedByEnv)
            {
                ImGui::BeginDisabled();
                ImGui::Checkbox("关闭Counter回读", &disableReadback);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("ENV锁定");
            }
            else if (ImGui::Checkbox("关闭Counter回读", &disableReadback))
            {
                changed = true;
            }

            if (changed)
            {
                ParticleSystemGPU::SetABConfigFromUI(forceGL, disableReadback);
                ab = ParticleSystemGPU::GetABConfigSnapshot();
            }

            ImGui::TextDisabled("来源: ForceGL=%s, DisableReadback=%s", ABSourceLabel(ab.ForceGLSource),
                                ABSourceLabel(ab.DisableReadbackSource));
        }

        // 物理设置（已移除 Custom 后端，强制使用 Bullet）
        ImGui::Separator();
        ImGui::Text("物理设置");
        {
            ImGui::TextDisabled("物理后端: Bullet3");
        }
        ImGui::Checkbox("显示碰撞体", m_ShowPhysicsColliders);
        ImGui::Checkbox("显示 Mesh SDF 体素边界", m_ShowMeshSDFBounds);

        if (m_ReadOnly)
            ImGui::EndDisabled();

        ImGui::End();
    }
} // namespace Engine
