#include "Panels/PropertiesPanel.h"
#include "Scene/SceneCamera.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Core/FileDialogs.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>

#include <filesystem>

namespace Engine
{

    template <typename T, typename UIFunction>
    void PropertiesPanel::DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction, bool removable)
    {
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                                                 ImGuiTreeNodeFlags_SpanAvailWidth |
                                                 ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;

        if (!entity.HasComponent<T>())
            return;

        auto& component = entity.GetComponent<T>();
        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4, 4});
        float lineHeight = ImGui::GetFrameHeight();
        ImGui::Separator();
        bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(T).hash_code()), treeNodeFlags, "%s",
                                      name.c_str());
        ImGui::PopStyleVar();

        bool removeComponent = false;
        if (removable)
        {
            ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
            if (ImGui::Button("+", ImVec2{lineHeight, lineHeight}))
            {
                ImGui::OpenPopup("ComponentSettings");
            }

            if (ImGui::BeginPopup("ComponentSettings"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    removeComponent = true;
                ImGui::EndPopup();
            }
        }

        if (open)
        {
            uiFunction(component);
            ImGui::TreePop();
        }

        if (removeComponent)
            entity.RemoveComponent<T>();
    }

    void PropertiesPanel::DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue,
                                          float columnWidth)
    {
        ImGuiIO& io = ImGui::GetIO();
        auto boldFont = io.Fonts->Fonts[0]; // Default font (bold can be added later)

        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::Text("%s", label.c_str());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 0});

        float lineHeight = ImGui::GetFrameHeight();
        ImVec2 buttonSize = {lineHeight + 3.0f, lineHeight};

        // X
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.9f, 0.2f, 0.2f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
        ImGui::PushFont(boldFont);
        if (ImGui::Button("X", buttonSize))
            values.x = resetValue;
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Y
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.2f, 0.7f, 0.2f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.3f, 0.8f, 0.3f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.2f, 0.7f, 0.2f, 1.0f});
        ImGui::PushFont(boldFont);
        if (ImGui::Button("Y", buttonSize))
            values.y = resetValue;
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Z
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.35f, 0.9f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
        ImGui::PushFont(boldFont);
        if (ImGui::Button("Z", buttonSize))
            values.z = resetValue;
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();

        ImGui::PopStyleVar();
        ImGui::Columns(1);

        ImGui::PopID();
    }

    void PropertiesPanel::OnImGuiRender(Entity selectedEntity)
    {
        ImGui::Begin("属性");

        if (selectedEntity)
        {
            DrawComponents(selectedEntity);
        }

        ImGui::End();
    }

    void PropertiesPanel::DrawComponents(Entity entity)
    {
        // Tag (entity name)
        if (entity.HasComponent<TagComponent>())
        {
            auto& tag = entity.GetComponent<TagComponent>().Tag;

            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            std::strncpy(buffer, tag.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("##Tag", buffer, sizeof(buffer)))
            {
                tag = std::string(buffer);
            }
        }
        ImGui::SameLine();
        ImGui::PushItemWidth(-1);

        if (ImGui::Button("添加组件"))
            ImGui::OpenPopup("AddComponent");

        if (ImGui::BeginPopup("AddComponent"))
        {
            if (!entity.HasComponent<CameraComponent>())
            {
                if (ImGui::MenuItem("相机"))
                {
                    entity.AddComponent<CameraComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!entity.HasComponent<MeshRendererComponent>())
            {
                if (ImGui::MenuItem("网格渲染器"))
                {
                    entity.AddComponent<MeshRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!entity.HasComponent<LightComponent>())
            {
                if (ImGui::MenuItem("灯光"))
                {
                    entity.AddComponent<LightComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }
        ImGui::PopItemWidth();

        // Transform
        DrawComponent<TransformComponent>("变换", entity, [this](auto& component)
        {
            DrawVec3Control("位移", component.Translation);
            glm::vec3 rotation = glm::degrees(component.Rotation);
            DrawVec3Control("旋转", rotation);
            component.Rotation = glm::radians(rotation);
            DrawVec3Control("缩放", component.Scale, 1.0f);
        }, false);

        // Camera
        DrawComponent<CameraComponent>("相机", entity, [](auto& component)
        {
            auto& camera = component.Camera;

            ImGui::Checkbox("主相机", &component.Primary);

            const char* projectionTypeStrings[] = {"透视", "正交"};
            const char* currentProjectionTypeString =
                projectionTypeStrings[static_cast<int>(camera.GetProjectionType())];

            if (ImGui::BeginCombo("投影方式", currentProjectionTypeString))
            {
                for (int i = 0; i < 2; i++)
                {
                    bool isSelected = currentProjectionTypeString == projectionTypeStrings[i];
                    if (ImGui::Selectable(projectionTypeStrings[i], isSelected))
                    {
                        currentProjectionTypeString = projectionTypeStrings[i];
                        camera.SetProjectionType(static_cast<SceneCamera::ProjectionType>(i));
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
            {
                float perspectiveFOV = glm::degrees(camera.GetPerspectiveVerticalFOV());
                if (ImGui::DragFloat("垂直FOV", &perspectiveFOV, 1.0f, 1.0f, 179.0f))
                    camera.SetPerspectiveVerticalFOV(glm::radians(std::clamp(perspectiveFOV, 1.0f, 179.0f)));

                float perspectiveNear = camera.GetPerspectiveNearClip();
                if (ImGui::DragFloat("近平面", &perspectiveNear, 0.01f, 0.001f, 0.0f))
                {
                    perspectiveNear = std::max(perspectiveNear, 0.001f);
                    camera.SetPerspectiveNearClip(perspectiveNear);
                }

                float perspectiveFar = camera.GetPerspectiveFarClip();
                if (ImGui::DragFloat("远平面", &perspectiveFar, 1.0f, 0.0f, 0.0f))
                {
                    perspectiveFar = std::max(perspectiveFar, camera.GetPerspectiveNearClip() + 0.1f);
                    camera.SetPerspectiveFarClip(perspectiveFar);
                }
            }

            if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
            {
                float orthoSize = camera.GetOrthographicSize();
                if (ImGui::DragFloat("大小", &orthoSize, 0.1f, 0.0f, 0.0f))
                {
                    orthoSize = std::max(orthoSize, 0.01f);
                    camera.SetOrthographicSize(orthoSize);
                }

                float orthoNear = camera.GetOrthographicNearClip();
                if (ImGui::DragFloat("近平面", &orthoNear, 0.1f))
                    camera.SetOrthographicNearClip(orthoNear);

                float orthoFar = camera.GetOrthographicFarClip();
                if (ImGui::DragFloat("远平面", &orthoFar, 0.1f))
                {
                    orthoFar = std::max(orthoFar, camera.GetOrthographicNearClip() + 0.1f);
                    camera.SetOrthographicFarClip(orthoFar);
                }

                ImGui::Checkbox("固定宽高比", &component.FixedAspectRatio);
            }
        });

        // Light
        DrawComponent<LightComponent>("灯光", entity, [](auto& component)
        {
            const char* lightTypeStrings[] = {"方向光", "点光源", "聚光灯"};
            const char* currentLightTypeString = lightTypeStrings[static_cast<int>(component.Type)];

            if (ImGui::BeginCombo("灯光类型", currentLightTypeString))
            {
                for (int i = 0; i < 3; i++)
                {
                    bool isSelected = (static_cast<int>(component.Type) == i);
                    if (ImGui::Selectable(lightTypeStrings[i], isSelected))
                        component.Type = static_cast<LightComponent::LightType>(i);
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::ColorEdit3("颜色", glm::value_ptr(component.Color));
            ImGui::DragFloat("强度", &component.Intensity, 0.05f, 0.0f, 100.0f, "%.2f");

            if (component.Type == LightComponent::LightType::Directional)
            {
                ImGui::Separator();
                ImGui::Checkbox("投射阴影", &component.CastShadows);
            }

            if (component.Type == LightComponent::LightType::Point ||
                component.Type == LightComponent::LightType::Spot)
            {
                ImGui::Separator();
                ImGui::Text("衰减");
                ImGui::DragFloat("常数项", &component.Constant, 0.01f, 0.001f, 10.0f, "%.3f");
                ImGui::DragFloat("线性项", &component.Linear, 0.001f, 0.0f, 1.0f, "%.4f");
                ImGui::DragFloat("二次项", &component.Quadratic, 0.001f, 0.0f, 1.0f, "%.4f");
            }

            if (component.Type == LightComponent::LightType::Spot)
            {
                ImGui::Separator();
                ImGui::Text("锥角");
                float innerDeg = glm::degrees(component.InnerCutoff);
                float outerDeg = glm::degrees(component.OuterCutoff);
                if (ImGui::DragFloat("内锥角", &innerDeg, 0.1f, 0.0f, 89.0f, "%.1f°"))
                    component.InnerCutoff = glm::radians(innerDeg);
                if (ImGui::DragFloat("外锥角", &outerDeg, 0.1f, 0.0f, 89.0f, "%.1f°"))
                    component.OuterCutoff = glm::radians(outerDeg);
                // Clamp: outer >= inner
                if (component.OuterCutoff < component.InnerCutoff)
                    component.OuterCutoff = component.InnerCutoff;
            }
        });

        // Mesh Renderer
        DrawComponent<MeshRendererComponent>("网格渲染器", entity, [](auto& component)
        {
            ImGui::ColorEdit4("颜色", glm::value_ptr(component.Color));

            // Mesh primitive selection
            const char* currentMeshLabel = "None";
            if (component.MeshData)
            {
                const std::string& meshType = component.MeshData->GetMeshType();
                if (!meshType.empty())
                    currentMeshLabel = meshType.c_str();
            }

            if (ImGui::BeginCombo("网格", currentMeshLabel))
            {
                if (ImGui::Selectable("Cube", component.MeshData && component.MeshData->GetMeshType() == "Cube"))
                {
                    component.MeshData = Mesh::CreateCube();
                    component.ModelPath.clear();
                }
                if (ImGui::Selectable("Plane", component.MeshData && component.MeshData->GetMeshType() == "Plane"))
                {
                    component.MeshData = Mesh::CreatePlane();
                    component.ModelPath.clear();
                }
                if (ImGui::Selectable("Sphere", component.MeshData && component.MeshData->GetMeshType() == "Sphere"))
                {
                    component.MeshData = Mesh::CreateSphere();
                    component.ModelPath.clear();
                }
                ImGui::EndCombo();
            }

            // Import model button
            if (ImGui::Button("导入模型..."))
            {
                std::string absPath = FileDialogs::OpenFile("*.obj;*.fbx;*.gltf;*.glb", "3D模型文件");
                if (!absPath.empty())
                {
                    std::error_code ec;
                    std::filesystem::path relative = std::filesystem::relative(absPath, std::filesystem::current_path(), ec);
                    std::string relStr = ec ? absPath : relative.string();

                    if (relStr.find("..") != std::string::npos)
                    {
                        ENGINE_WARN("模型必须位于项目目录内: {0}", relStr);
                    }
                    else
                    {
                        auto mesh = Mesh::CreateFromFile(relStr);
                        if (mesh)
                        {
                            component.MeshData = mesh;
                            component.ModelPath = relStr;

                            // Auto-detect: if model has submesh textures, clear the component-level texture
                            // so per-submesh textures take effect
                            bool hasSubMeshTex = false;
                            for (const auto& sub : mesh->GetSubMeshes())
                            {
                                if (sub.DiffuseTexture)
                                {
                                    hasSubMeshTex = true;
                                    break;
                                }
                            }
                            if (hasSubMeshTex)
                            {
                                component.DiffuseTexture.reset();
                                component.TexturePath.clear();
                            }
                        }
                    }
                }
            }

            // Show model path if loaded
            if (component.MeshData && component.MeshData->GetMeshType() == "Model")
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", component.MeshData->GetModelPath().c_str());
            }

            ImGui::Separator();
            ImGui::Text("材质");
            ImGui::DragFloat("高光度", &component.Shininess, 1.0f, 1.0f, 256.0f, "%.0f");

            // Texture path
            char texPathBuf[256];
            memset(texPathBuf, 0, sizeof(texPathBuf));
            std::strncpy(texPathBuf, component.TexturePath.c_str(), sizeof(texPathBuf) - 1);
            if (ImGui::InputText("纹理路径", texPathBuf, sizeof(texPathBuf)))
            {
                component.TexturePath = std::string(texPathBuf);
            }
            ImGui::SameLine();
            if (ImGui::Button("浏览..."))
            {
                std::string absPath = FileDialogs::OpenFile("*.png;*.jpg;*.jpeg;*.bmp;*.tga", "图片文件");
                if (!absPath.empty())
                {
                    std::error_code ec;
                    std::filesystem::path relative = std::filesystem::relative(absPath, std::filesystem::current_path(), ec);
                    std::string relStr = ec ? absPath : relative.string();

                    // Reject paths that escape project root (contain "..")
                    if (relStr.find("..") != std::string::npos)
                    {
                        ENGINE_WARN("纹理必须位于项目目录内: {0}", relStr);
                    }
                    else
                    {
                        component.TexturePath = relStr;
                        component.DiffuseTexture = Texture2D::Create(component.TexturePath);
                    }
                }
            }

            if (ImGui::Button("加载纹理"))
            {
                if (!component.TexturePath.empty())
                {
                    component.DiffuseTexture = Texture2D::Create(component.TexturePath);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("清除纹理"))
            {
                component.DiffuseTexture.reset();
                component.TexturePath.clear();
            }

            ImGui::DragFloat("平铺 X", &component.Tiling.x, 0.1f, 0.01f, 100.0f, "%.2f");
            ImGui::DragFloat("平铺 Y", &component.Tiling.y, 0.1f, 0.01f, 100.0f, "%.2f");
        });
    }

} // namespace Engine
