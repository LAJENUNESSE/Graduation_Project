#include "Panels/PropertiesPanel.h"
#include "Scene/SceneCamera.h"
#include "Renderer/Mesh.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>

namespace Engine
{

    template <typename T, typename UIFunction>
    void PropertiesPanel::DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction)
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

        ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
        if (ImGui::Button("+", ImVec2{lineHeight, lineHeight}))
        {
            ImGui::OpenPopup("ComponentSettings");
        }

        bool removeComponent = false;
        if (ImGui::BeginPopup("ComponentSettings"))
        {
            if (ImGui::MenuItem("Remove Component"))
                removeComponent = true;
            ImGui::EndPopup();
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
        });

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
                if (ImGui::DragFloat("垂直FOV", &perspectiveFOV))
                    camera.SetPerspectiveVerticalFOV(glm::radians(perspectiveFOV));

                float perspectiveNear = camera.GetPerspectiveNearClip();
                if (ImGui::DragFloat("近平面", &perspectiveNear))
                    camera.SetPerspectiveNearClip(perspectiveNear);

                float perspectiveFar = camera.GetPerspectiveFarClip();
                if (ImGui::DragFloat("远平面", &perspectiveFar))
                    camera.SetPerspectiveFarClip(perspectiveFar);
            }

            if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
            {
                float orthoSize = camera.GetOrthographicSize();
                if (ImGui::DragFloat("大小", &orthoSize))
                    camera.SetOrthographicSize(orthoSize);

                float orthoNear = camera.GetOrthographicNearClip();
                if (ImGui::DragFloat("近平面", &orthoNear))
                    camera.SetOrthographicNearClip(orthoNear);

                float orthoFar = camera.GetOrthographicFarClip();
                if (ImGui::DragFloat("远平面", &orthoFar))
                    camera.SetOrthographicFarClip(orthoFar);

                ImGui::Checkbox("固定宽高比", &component.FixedAspectRatio);
            }
        });

        // Mesh Renderer
        DrawComponent<MeshRendererComponent>("Mesh Renderer", entity, [](auto& component)
        {
            ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));

            // Mesh primitive selection — determine current type from Mesh::GetMeshType()
            const char* currentMeshLabel = "None";
            if (component.MeshData)
            {
                const std::string& meshType = component.MeshData->GetMeshType();
                if (!meshType.empty())
                    currentMeshLabel = meshType.c_str();
            }

            if (ImGui::BeginCombo("Mesh", currentMeshLabel))
            {
                if (ImGui::Selectable("Cube", component.MeshData && component.MeshData->GetMeshType() == "Cube"))
                    component.MeshData = Mesh::CreateCube();
                if (ImGui::Selectable("Plane", component.MeshData && component.MeshData->GetMeshType() == "Plane"))
                    component.MeshData = Mesh::CreatePlane();
                if (ImGui::Selectable("Sphere", component.MeshData && component.MeshData->GetMeshType() == "Sphere"))
                    component.MeshData = Mesh::CreateSphere();
                ImGui::EndCombo();
            }
        });
    }

} // namespace Engine
