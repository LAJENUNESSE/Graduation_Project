#include "EditorViewportController.h"

#include "Core/Application.h"
#include "ImGui/ImGuiLayer.h"
#include "Scene/Scene.h"

#include <imgui.h>

#include <algorithm>

namespace Engine
{
    namespace
    {
        glm::vec2 GetFramebufferScale()
        {
            ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
            return {scale.x > 0.0f ? scale.x : 1.0f, scale.y > 0.0f ? scale.y : 1.0f};
        }
    } // namespace

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

        FramebufferSpecification pickingSpec;
        pickingSpec.Attachments = {FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER,
                                   FramebufferTextureFormat::DEPTH24STENCIL8};
        pickingSpec.Width = width;
        pickingSpec.Height = height;
        m_PickingFramebuffer = Framebuffer::Create(pickingSpec);

        m_Context.Size = {static_cast<float>(width), static_cast<float>(height)};
        m_Context.RenderSize = m_Context.Size;
        m_TargetSize = m_Context.RenderSize;
        m_EditorCamera = EditorCamera(45.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 1000.0f);
        m_EditorCamera.SetViewportSize(static_cast<float>(width), static_cast<float>(height));
    }

    void EditorViewportController::OnUpdate(Timestep ts, Scene& activeScene)
    {
        FramebufferSpecification spec = m_Framebuffer->GetSpecification();
        if (m_TargetSize.x > 0.0f && m_TargetSize.y > 0.0f &&
            (spec.Width != static_cast<uint32_t>(m_TargetSize.x) ||
             spec.Height != static_cast<uint32_t>(m_TargetSize.y)))
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                uint32_t width = static_cast<uint32_t>(m_TargetSize.x);
                uint32_t height = static_cast<uint32_t>(m_TargetSize.y);

                m_Framebuffer->Resize(width, height);
                m_PickingFramebuffer->Resize(width, height);
                m_EditorCamera.SetViewportSize(m_TargetSize.x, m_TargetSize.y);
                activeScene.OnViewportResize(width, height);
                if (m_OnResize)
                    m_OnResize(width, height);
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

        m_Context.Focused = ImGui::IsWindowFocused();

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        m_Context.Size = {std::max(viewportPanelSize.x, 32.0f), std::max(viewportPanelSize.y, 32.0f)};

        glm::vec2 framebufferScale = GetFramebufferScale();
        m_TargetSize = {std::max(m_Context.Size.x * framebufferScale.x, 32.0f),
                        std::max(m_Context.Size.y * framebufferScale.y, 32.0f)};

        const auto& spec = m_Framebuffer->GetSpecification();
        float sourceWidth = static_cast<float>(std::max(spec.Width, 1u));
        float sourceHeight = static_cast<float>(std::max(spec.Height, 1u));
        m_Context.RenderSize = {sourceWidth, sourceHeight};

        glm::vec2 sourceDisplaySize = {sourceWidth / framebufferScale.x, sourceHeight / framebufferScale.y};
        float scale = std::min(m_Context.Size.x / sourceDisplaySize.x, m_Context.Size.y / sourceDisplaySize.y);
        scale = std::max(scale, 0.0f);

        glm::vec2 imageSize = {sourceDisplaySize.x * scale, sourceDisplaySize.y * scale};
        ImVec2 cursorPos = ImGui::GetCursorPos();
        ImVec2 imageOffset((m_Context.Size.x - imageSize.x) * 0.5f, (m_Context.Size.y - imageSize.y) * 0.5f);
        ImGui::SetCursorPos(ImVec2(cursorPos.x + imageOffset.x, cursorPos.y + imageOffset.y));

        ImVec2 imageScreenPos = ImGui::GetCursorScreenPos();
        m_Context.Bounds[0] = {imageScreenPos.x, imageScreenPos.y};
        m_Context.Bounds[1] = {imageScreenPos.x + imageSize.x, imageScreenPos.y + imageSize.y};

        ImVec2 mousePos = ImGui::GetMousePos();
        m_Context.Hovered = ImGui::IsWindowHovered() && mousePos.x >= m_Context.Bounds[0].x &&
                            mousePos.x <= m_Context.Bounds[1].x && mousePos.y >= m_Context.Bounds[0].y &&
                            mousePos.y <= m_Context.Bounds[1].y;
        Application::Get().GetImGuiLayer()->SetBlockEvents(!m_Context.Hovered);

        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID(0);
        ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(textureID)), ImVec2(imageSize.x, imageSize.y),
                     ImVec2(0, 1), ImVec2(1, 0));

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
