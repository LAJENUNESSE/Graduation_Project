#include "EditorViewportController.h"

#include "Core/Application.h"
#include "ImGui/ImGuiLayer.h"
#include "Scene/Scene.h"

#include <imgui.h>

#include <algorithm>

namespace Engine
{

    void EditorViewportController::Initialize(uint32_t width, uint32_t height)
    {
        FramebufferSpecification fbSpec;
        fbSpec.Attachments = {FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER,
                              FramebufferTextureFormat::DEPTH24STENCIL8};
        fbSpec.Width = width;
        fbSpec.Height = height;
        m_Framebuffer = Framebuffer::Create(fbSpec);

        FramebufferSpecification hdrSpec;
        hdrSpec.Attachments = {FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
                               FramebufferTextureFormat::DEPTH_COMPONENT};
        hdrSpec.Width = width;
        hdrSpec.Height = height;
        m_HDRFramebuffer = Framebuffer::Create(hdrSpec);

        m_Context.Size = {static_cast<float>(width), static_cast<float>(height)};
        m_EditorCamera = EditorCamera(45.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 1000.0f);
        m_EditorCamera.SetViewportSize(static_cast<float>(width), static_cast<float>(height));
    }

    void EditorViewportController::OnUpdate(Timestep ts, Scene& activeScene)
    {
        FramebufferSpecification spec = m_Framebuffer->GetSpecification();
        if (m_Context.Size.x > 0.0f && m_Context.Size.y > 0.0f &&
            (spec.Width != static_cast<uint32_t>(m_Context.Size.x) ||
             spec.Height != static_cast<uint32_t>(m_Context.Size.y)))
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                uint32_t width = static_cast<uint32_t>(m_Context.Size.x);
                uint32_t height = static_cast<uint32_t>(m_Context.Size.y);

                m_Framebuffer->Resize(width, height);
                m_HDRFramebuffer->Resize(width, height);
                m_EditorCamera.SetViewportSize(m_Context.Size.x, m_Context.Size.y);
                activeScene.OnViewportResize(width, height);
            }
        }

        m_EditorCamera.OnUpdate(ts, m_Context.Hovered);
    }

    void EditorViewportController::OnEvent(Event& event)
    {
        m_EditorCamera.OnEvent(event);
    }

    EditorViewportContext EditorViewportController::BeginViewportWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("视口");

        ImVec2 minRegion = ImGui::GetWindowContentRegionMin();
        ImVec2 maxRegion = ImGui::GetWindowContentRegionMax();
        ImVec2 offset = ImGui::GetWindowPos();
        m_Context.Bounds[0] = {minRegion.x + offset.x, minRegion.y + offset.y};
        m_Context.Bounds[1] = {maxRegion.x + offset.x, maxRegion.y + offset.y};

        m_Context.Focused = ImGui::IsWindowFocused();
        m_Context.Hovered = ImGui::IsWindowHovered();
        Application::Get().GetImGuiLayer()->SetBlockEvents(!m_Context.Hovered);

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        m_Context.Size = {
            std::max(viewportPanelSize.x, 32.0f),
            std::max(viewportPanelSize.y, 32.0f)
        };

        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID(0);
        ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(textureID)),
                     ImVec2(m_Context.Size.x, m_Context.Size.y), ImVec2(0, 1), ImVec2(1, 0));

        return m_Context;
    }

    void EditorViewportController::EndViewportWindow()
    {
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorViewportController::ApplyMSAASamples(uint32_t samples)
    {
        FramebufferSpecification spec = m_HDRFramebuffer->GetSpecification();
        spec.Samples = samples;
        m_HDRFramebuffer = Framebuffer::Create(spec);
    }

} // namespace Engine
