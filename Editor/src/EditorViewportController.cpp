#include "EditorViewportController.h"

#include "Core/Application.h"
#include "Renderer/RendererAPI.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Debug/PerformanceMonitor.h"
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
        fbSpec.Width       = width;
        fbSpec.Height      = height;
        m_Framebuffer      = Framebuffer::Create(fbSpec);

        FramebufferSpecification hdrSpec;
        hdrSpec.Attachments = {FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
                               FramebufferTextureFormat::DEPTH_COMPONENT};
        hdrSpec.Width       = width;
        hdrSpec.Height      = height;
        m_HDRFramebuffer    = Framebuffer::Create(hdrSpec);

        FramebufferSpecification pickingSpec;
        pickingSpec.Attachments = {FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER,
                                   FramebufferTextureFormat::DEPTH24STENCIL8};
        pickingSpec.Width       = width;
        pickingSpec.Height      = height;
        m_PickingFramebuffer    = Framebuffer::Create(pickingSpec);

        m_Context.Size       = {static_cast<float>(width), static_cast<float>(height)};
        m_Context.RenderSize = m_Context.Size;
        m_TargetSize         = m_Context.RenderSize;
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
                uint32_t width  = static_cast<uint32_t>(m_TargetSize.x);
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
        m_Context.Size           = {std::max(viewportPanelSize.x, 32.0f), std::max(viewportPanelSize.y, 32.0f)};

        glm::vec2 framebufferScale = GetFramebufferScale();
        m_TargetSize               = {std::max(m_Context.Size.x * framebufferScale.x, 32.0f),
                                      std::max(m_Context.Size.y * framebufferScale.y, 32.0f)};

        const auto& spec         = m_Framebuffer->GetSpecification();
        float       sourceWidth  = static_cast<float>(std::max(spec.Width, 1u));
        float       sourceHeight = static_cast<float>(std::max(spec.Height, 1u));
        m_Context.RenderSize     = {sourceWidth, sourceHeight};

        glm::vec2 sourceDisplaySize = {sourceWidth / framebufferScale.x, sourceHeight / framebufferScale.y};
        float     scale = std::min(m_Context.Size.x / sourceDisplaySize.x, m_Context.Size.y / sourceDisplaySize.y);
        scale           = std::max(scale, 0.0f);

        glm::vec2 imageSize = {sourceDisplaySize.x * scale, sourceDisplaySize.y * scale};
        ImVec2    cursorPos = ImGui::GetCursorPos();
        ImVec2    imageOffset((m_Context.Size.x - imageSize.x) * 0.5f, (m_Context.Size.y - imageSize.y) * 0.5f);
        ImGui::SetCursorPos(ImVec2(cursorPos.x + imageOffset.x, cursorPos.y + imageOffset.y));

        ImVec2 imageScreenPos = ImGui::GetCursorScreenPos();
        m_Context.Bounds[0]   = {imageScreenPos.x, imageScreenPos.y};
        m_Context.Bounds[1]   = {imageScreenPos.x + imageSize.x, imageScreenPos.y + imageSize.y};

        ImVec2 mousePos   = ImGui::GetMousePos();
        m_Context.Hovered = ImGui::IsWindowHovered() && mousePos.x >= m_Context.Bounds[0].x &&
                            mousePos.x <= m_Context.Bounds[1].x && mousePos.y >= m_Context.Bounds[0].y &&
                            mousePos.y <= m_Context.Bounds[1].y;
        Application::Get().GetImGuiLayer()->SetBlockEvents(!m_Context.Hovered);

        uint32_t    textureID   = m_Framebuffer->GetColorAttachmentRendererID(0);
        ImTextureID viewportTex = static_cast<ImTextureID>(static_cast<uintptr_t>(textureID));
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            // Phase 8.2：Vulkan path 以 VkDescriptorSet（void*）作为 ImTextureID
            void* descriptorSet =
                VulkanContext::Get()->GetImGuiTextureForView(m_Framebuffer->GetColorAttachmentViewHandle(0));
            viewportTex = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(descriptorSet));
        }
        ImGui::Image(viewportTex, ImVec2(imageSize.x, imageSize.y), ImVec2(0, 1), ImVec2(1, 0));

        // FPS overlay drawn directly on the viewport via ImDrawList (simulated frosted-glass blur)
        if (m_ShowFpsOverlay && imageSize.x > 0.0f && imageSize.y > 0.0f)
        {
            auto&       pm   = PerformanceMonitor::Get();
            ImDrawList* dl   = ImGui::GetWindowDrawList();
            ImFont*     font = ImGui::GetFont();

            char fps_buf[32], ms_buf[32];
            snprintf(fps_buf, sizeof(fps_buf), "FPS: %.1f", pm.GetFPS());
            snprintf(ms_buf, sizeof(ms_buf), "%.2f ms", pm.GetFrameTimeMs());

            const float fontSize = ImGui::GetFontSize();
            const float lineGap  = 4.0f;
            const float padX     = 14.0f;
            const float padY     = 8.0f;
            const float rounding = 6.0f;

            ImVec2 szFps = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fps_buf);
            ImVec2 szMs  = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, ms_buf);

            float boxW = std::max(szFps.x, szMs.x) + padX * 2.0f;
            float boxH = szFps.y + lineGap + szMs.y + padY * 2.0f;

            // Anchor: top-left of the rendered image + margin
            ImVec2 bMin = ImVec2(imageScreenPos.x + 12.0f, imageScreenPos.y + 12.0f);
            ImVec2 bMax = ImVec2(bMin.x + boxW, bMin.y + boxH);

            // Simulated gaussian blur: draw concentric dilated rects with decreasing opacity
            for (int i = 5; i >= 1; --i)
            {
                float  d   = static_cast<float>(i) * 2.0f;
                ImVec2 mn  = ImVec2(bMin.x - d, bMin.y - d);
                ImVec2 mx  = ImVec2(bMax.x + d, bMax.y + d);
                ImU32  col = IM_COL32(8, 8, 8, static_cast<int>(20 + i * 8));
                dl->AddRectFilled(mn, mx, col, rounding + d * 0.6f);
            }

            // Solid core background
            dl->AddRectFilled(bMin, bMax, IM_COL32(12, 12, 12, 195), rounding);
            // Subtle inner highlight (top edge)
            dl->AddRectFilled(bMin, ImVec2(bMax.x, bMin.y + 1.0f), IM_COL32(255, 255, 255, 18), rounding);
            // Thin border
            dl->AddRect(bMin, bMax, IM_COL32(80, 80, 80, 90), rounding, 0, 1.0f);

            // Centered FPS text (green)
            ImVec2 fpsTL = ImVec2(bMin.x + (boxW - szFps.x) * 0.5f, bMin.y + padY);
            dl->AddText(font, fontSize, fpsTL, IM_COL32(60, 220, 60, 230), fps_buf);

            // Centered ms text (muted)
            ImVec2 msTL = ImVec2(bMin.x + (boxW - szMs.x) * 0.5f, fpsTL.y + szFps.y + lineGap);
            dl->AddText(font, fontSize, msTL, IM_COL32(190, 190, 190, 200), ms_buf);
        }

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
        spec.Samples                  = samples;
        m_HDRFramebuffer              = Framebuffer::Create(spec);
    }

} // namespace Engine
