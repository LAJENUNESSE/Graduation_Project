#include "EditorPanelCoordinator.h"

#include "Debug/PerformanceMonitor.h"
#include "Panels/AssetBrowserPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/PropertiesPanel.h"
#include "Panels/RenderSettingsPanel.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/ParticleSystemGPU.h"
#include "Scene/Entity.h"
#include "UndoSystem.h"

#include <imgui.h>

namespace Engine
{
    namespace
    {
        const char* ABSourceLabel(ParticleSystemGPU::ABConfigSource source)
        {
            return ParticleSystemGPU::ABConfigSourceLabel(source);
        }
    } // namespace

    void EditorPanelCoordinator::Initialize(SceneHierarchyPanel* hierarchyPanel,
                                            PropertiesPanel*     propertiesPanel,
                                            ConsolePanel*        consolePanel,
                                            AssetBrowserPanel*   assetBrowserPanel,
                                            RenderSettingsPanel* renderSettingsPanel,
                                            CommandHistory*      commandHistory)
    {
        m_HierarchyPanel      = hierarchyPanel;
        m_PropertiesPanel     = propertiesPanel;
        m_ConsolePanel        = consolePanel;
        m_AssetBrowserPanel   = assetBrowserPanel;
        m_RenderSettingsPanel = renderSettingsPanel;
        m_CommandHistory      = commandHistory;

        if (m_PropertiesPanel)
            m_PropertiesPanel->SetCommandHistory(commandHistory);
    }

    void EditorPanelCoordinator::ApplyScene(const Ref<Scene>& scene, bool clearCommandHistory)
    {
        if (m_HierarchyPanel)
            m_HierarchyPanel->SetContext(scene);
        if (m_AssetBrowserPanel)
            m_AssetBrowserPanel->SetContext(scene);
        if (m_RenderSettingsPanel)
            m_RenderSettingsPanel->SetScene(scene);
        if (m_PropertiesPanel)
            m_PropertiesPanel->SetScene(scene);

        ClearSelection();

        if (clearCommandHistory && m_CommandHistory)
            m_CommandHistory->Clear();
    }

    void EditorPanelCoordinator::RenderPanels()
    {
        if (m_HierarchyPanel)
            m_HierarchyPanel->OnImGuiRender();
        if (m_PropertiesPanel)
            m_PropertiesPanel->OnImGuiRender(GetPrimarySelection());
        if (m_ConsolePanel)
            m_ConsolePanel->OnImGuiRender();
        if (m_AssetBrowserPanel)
            m_AssetBrowserPanel->OnImGuiRender();
        if (m_RenderSettingsPanel)
            m_RenderSettingsPanel->OnImGuiRender();

        RenderStatsPanel();
    }

    void EditorPanelCoordinator::SetHDRFramebuffer(const Ref<Framebuffer>& framebuffer)
    {
        if (m_RenderSettingsPanel)
            m_RenderSettingsPanel->SetHDRFramebuffer(framebuffer);
    }

    void EditorPanelCoordinator::SetPanelsReadOnly(bool readOnly)
    {
        if (m_HierarchyPanel)
            m_HierarchyPanel->SetReadOnly(readOnly);
        if (m_PropertiesPanel)
            m_PropertiesPanel->SetReadOnly(readOnly);
        if (m_RenderSettingsPanel)
            m_RenderSettingsPanel->SetReadOnly(readOnly);
    }

    void EditorPanelCoordinator::SetSceneModifiedCallback(std::function<void()> cb)
    {
        if (m_PropertiesPanel)
            m_PropertiesPanel->SetSceneModifiedCallback(std::move(cb));
    }
    Entity EditorPanelCoordinator::GetPrimarySelection() const
    {
        return m_HierarchyPanel ? m_HierarchyPanel->GetSelectedEntity() : Entity{};
    }

    const std::vector<Entity>& EditorPanelCoordinator::GetSelectedEntities() const
    {
        static const std::vector<Entity> empty;
        return m_HierarchyPanel ? m_HierarchyPanel->GetSelectedEntities() : empty;
    }

    void EditorPanelCoordinator::SetPrimarySelection(Entity entity)
    {
        if (m_HierarchyPanel)
            m_HierarchyPanel->SetSelectedEntity(entity);
    }

    void EditorPanelCoordinator::ClearSelection()
    {
        if (m_HierarchyPanel)
            m_HierarchyPanel->ClearSelection();
    }

    void EditorPanelCoordinator::RenderStatsPanel()
    {
        if (!m_ShowStatsPanel)
            return;

        auto& pm = PerformanceMonitor::Get();

        ImGui::Begin("性能监控", &m_ShowStatsPanel);

        ImGui::Text("FPS: %.1f", pm.GetFPS());
        ImGui::Text("帧时间: %.2f ms", pm.GetFrameTimeMs());

        ImGui::Separator();
        ImGui::Text("CPU 耗时:");
        ImGui::Text("  阴影Pass:  %.3f ms", pm.GetShadowPassCpuMs());
        ImGui::Text("  场景渲染:  %.3f ms", pm.GetSceneRenderCpuMs());
        ImGui::Text("  ImGui:     %.3f ms", pm.GetImGuiCpuMs());
        ImGui::Text("  PollEvents:  %.3f ms", pm.GetPollEventsCpuMs());
        ImGui::Text("  SwapBuffers: %.3f ms", pm.GetSwapBuffersCpuMs());
        ImGui::Text("  主导阶段: %s", pm.GetFrameDominantStageLabel());
        ImGui::Text("  刷新率: %.1f Hz (%.3f ms)", pm.GetRefreshHz(), pm.GetRefreshPeriodMs());
        if (pm.GetSwapBurstId() > 0)
        {
            ImGui::Text("  Swap诊断: #%u%s, len=%u, max=%.3f ms, miss=%u", pm.GetSwapBurstId(),
                        pm.IsSwapBurstActive() ? "(活跃)" : "", pm.GetSwapBurstLen(), pm.GetSwapBurstMaxMs(),
                        pm.GetSwapBurstMissedVBlank());
        }
        else
        {
            ImGui::Text("  Swap诊断: 暂无异常段");
        }

        ImGui::Separator();
        ImGui::Text("GPU 耗时 (上一帧):");
        ImGui::Text("  阴影Pass:  %.3f ms", pm.GetShadowPassGpuMs());
        ImGui::Text("  场景渲染:  %.3f ms", pm.GetSceneRenderGpuMs());
        ImGui::Text("  粒子Compute: %.3f ms [%s]", pm.GetParticleComputeGpuMs(),
                    pm.IsParticleUsingCuda() ? "CUDA" : "GL");
        ImGui::Text("  粒子CPU分段: Map=%.3f ms, Unmap=%.3f ms, Readback=%.3f ms", pm.GetParticleCudaMapAllCpuMs(),
                    pm.GetParticleCudaUnmapAllCpuMs(), pm.GetParticleCounterReadbackCpuMs());
        {
            const auto ab = ParticleSystemGPU::GetABConfigSnapshot();
            ImGui::Text("  粒子AB: ForceGL=%d (%s), DisableReadback=%d (%s)", ab.ForceGL ? 1 : 0,
                        ABSourceLabel(ab.ForceGLSource), ab.DisableCounterReadback ? 1 : 0,
                        ABSourceLabel(ab.DisableReadbackSource));
        }
        if (pm.IsFluidActive())
            ImGui::Text("  流体Compute: %.3f ms [%s]", pm.GetFluidComputeGpuMs(),
                        pm.IsFluidUsingCuda() ? "CUDA" : "GL");

        ImGui::Separator();
        const auto& stats = pm.GetStats();
        ImGui::Text("渲染统计:");
        ImGui::Text("  Draw Calls: %u", stats.DrawCalls);
        ImGui::Text("  顶点数: %u", stats.Vertices);
        ImGui::Text("  三角形: %u", stats.Triangles);

        ImGui::Separator();
        ImGui::Text("帧时间历史:");
        ImGui::PlotLines("##FrameTime", pm.GetFrameTimeHistory(), PerformanceMonitor::FrameHistorySize,
                         pm.GetFrameTimeHistoryOffset(), nullptr, 0.0f, 33.3f, ImVec2(0, 80));

        ImGui::End();
    }

} // namespace Engine
