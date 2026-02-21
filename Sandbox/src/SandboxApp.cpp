#include "Engine.h"
#include "Core/EntryPoint.h"
#include "ImGui/ImGuiLayer.h"

#include <glad/gl.h>
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine
{

    class ExampleLayer : public Layer
    {
    public:
        ExampleLayer() : Layer("Example") {}

        void OnAttach() override
        {
            ENGINE_INFO("ExampleLayer 已挂载！");

            // ── 创建 Framebuffer ──
            FramebufferSpecification fbSpec;
            fbSpec.Width = 1280;
            fbSpec.Height = 720;
            m_Framebuffer = Framebuffer::Create(fbSpec);

            // ── 创建 3D 立方体 VAO ──
            m_CubeVA = VertexArray::Create();

            // clang-format off
            float vertices[] = {
                // 位置                 // 颜色
                // 前面 (z = +0.5)
                -0.5f, -0.5f,  0.5f,   1.0f, 0.0f, 0.0f,
                 0.5f, -0.5f,  0.5f,   0.0f, 1.0f, 0.0f,
                 0.5f,  0.5f,  0.5f,   0.0f, 0.0f, 1.0f,
                -0.5f,  0.5f,  0.5f,   1.0f, 1.0f, 0.0f,
                // 后面 (z = -0.5)
                -0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 1.0f,
                 0.5f, -0.5f, -0.5f,   0.0f, 1.0f, 1.0f,
                 0.5f,  0.5f, -0.5f,   1.0f, 1.0f, 1.0f,
                -0.5f,  0.5f, -0.5f,   0.5f, 0.5f, 0.5f,
            };
            // clang-format on

            auto vbo = VertexBuffer::Create(vertices, sizeof(vertices));
            vbo->SetLayout({
                {ShaderDataType::Float3, "a_Position"},
                {ShaderDataType::Float3, "a_Color"}
            });
            m_CubeVA->AddVertexBuffer(vbo);

            // clang-format off
            uint32_t indices[] = {
                // 前面
                0, 1, 2,  2, 3, 0,
                // 后面
                5, 4, 7,  7, 6, 5,
                // 左面
                4, 0, 3,  3, 7, 4,
                // 右面
                1, 5, 6,  6, 2, 1,
                // 上面
                3, 2, 6,  6, 7, 3,
                // 下面
                4, 5, 1,  1, 0, 4,
            };
            // clang-format on

            auto ibo = IndexBuffer::Create(indices, 36);
            m_CubeVA->SetIndexBuffer(ibo);

            // ── 创建着色器 ──
            std::string vertexSrc = R"(
                #version 330 core
                layout(location = 0) in vec3 a_Position;
                layout(location = 1) in vec3 a_Color;

                uniform mat4 u_ViewProjection;
                uniform mat4 u_Transform;

                out vec3 v_Color;

                void main() {
                    v_Color = a_Color;
                    gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
                }
            )";

            std::string fragmentSrc = R"(
                #version 330 core
                layout(location = 0) out vec4 color;

                in vec3 v_Color;

                void main() {
                    color = vec4(v_Color, 1.0);
                }
            )";

            m_Shader = Shader::Create("Cube", vertexSrc, fragmentSrc);

            // ── 设置相机 ──
            m_Camera = EditorCamera(45.0f, 1280.0f / 720.0f, 0.1f, 1000.0f);

            ENGINE_INFO("操作说明:");
            ENGINE_INFO("  Alt+左键: 旋转相机");
            ENGINE_INFO("  Alt+中键: 平移相机");
            ENGINE_INFO("  滚轮: 缩放");
            ENGINE_INFO("  ESC: 退出");
        }

        void OnUpdate(Timestep ts) override
        {
            m_Camera.OnUpdate(ts);

            // 渲染到 Framebuffer
            m_Framebuffer->Bind();
            RenderCommand::SetClearColor(m_ClearColor);
            RenderCommand::Clear();

            // 旋转立方体
            m_CubeRotation += ts.GetSeconds() * m_RotationSpeed;
            glm::mat4 transform = glm::rotate(glm::mat4(1.0f), glm::radians(m_CubeRotation), {0.5f, 1.0f, 0.0f});

            Renderer::BeginScene(m_Camera.GetViewProjection());
            Renderer::Submit(m_Shader, m_CubeVA, transform);
            Renderer::EndScene();

            m_Framebuffer->Unbind();
        }

        void OnImGuiRender() override
        {
            // ── Docking 全屏布局 ──
            static bool dockspaceOpen = true;
            static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

            ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                           ImGuiWindowFlags_NoNavFocus;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("DockSpace", &dockspaceOpen, windowFlags);
            ImGui::PopStyleVar(3);

            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
            {
                ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
                ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
            }

            // ── 菜单栏 ──
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("文件"))
                {
                    if (ImGui::MenuItem("退出"))
                    {
                        Application::Get().Close();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            // ── 视口面板 ──
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin("视口");

            // 当鼠标悬停在视口上时，让事件穿透到 EditorCamera（允许滚轮缩放等）
            m_ViewportHovered = ImGui::IsWindowHovered();
            Application::Get().GetImGuiLayer()->SetBlockEvents(!m_ViewportHovered);

            ImVec2 viewportSize = ImGui::GetContentRegionAvail();
            uint32_t newW = static_cast<uint32_t>(viewportSize.x);
            uint32_t newH = static_cast<uint32_t>(viewportSize.y);
            if (newW > 0 && newH > 0 &&
                (newW != static_cast<uint32_t>(m_ViewportSize.x) ||
                 newH != static_cast<uint32_t>(m_ViewportSize.y)))
            {
                m_ViewportSize = {viewportSize.x, viewportSize.y};
                m_Framebuffer->Resize(newW, newH);
                m_Camera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
            }

            uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
            ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(textureID)),
                         ImVec2(m_ViewportSize.x, m_ViewportSize.y), ImVec2(0, 1), ImVec2(1, 0));

            ImGui::End();
            ImGui::PopStyleVar();

            // ── 属性面板 ──
            ImGui::Begin("属性");
            ImGui::Text("帧率: %.1f FPS", io.Framerate);
            ImGui::Separator();
            ImGui::SliderFloat("旋转速度", &m_RotationSpeed, 0.0f, 200.0f);
            ImGui::ColorEdit3("清屏颜色", &m_ClearColor.x);
            ImGui::End();

            ImGui::End(); // DockSpace
        }

        void OnEvent(Event& event) override
        {
            m_Camera.OnEvent(event);

            EventDispatcher dispatcher(event);
            dispatcher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& e)
            {
                if (e.GetKeyCode() == 256) // ESC
                {
                    Application::Get().Close();
                }
                return false;
            });
        }

    private:
        Ref<Shader> m_Shader;
        Ref<VertexArray> m_CubeVA;
        Ref<Framebuffer> m_Framebuffer;
        EditorCamera m_Camera;

        glm::vec2 m_ViewportSize = {1280.0f, 720.0f};
        glm::vec4 m_ClearColor = {0.15f, 0.15f, 0.15f, 1.0f};
        float m_CubeRotation = 0.0f;
        float m_RotationSpeed = 50.0f;
        bool m_ViewportHovered = false;
    };

    class Sandbox : public Application
    {
    public:
        Sandbox()
        {
            PushLayer(new ExampleLayer());
        }

        ~Sandbox() override {}
    };

    Application* CreateApplication()
    {
        return new Sandbox();
    }

} // namespace Engine
