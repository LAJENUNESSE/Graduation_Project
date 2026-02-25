#include "Panels/PropertiesPanel.h"
#include "Scene/SceneCamera.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Core/FileDialogs.h"
#include "Core/Log.h"

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

            if (!entity.HasComponent<RigidBodyComponent>())
            {
                if (ImGui::MenuItem("刚体"))
                {
                    entity.AddComponent<RigidBodyComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!entity.HasComponent<BoxColliderComponent>())
            {
                if (ImGui::MenuItem("盒碰撞器"))
                {
                    entity.AddComponent<BoxColliderComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!entity.HasComponent<SphereColliderComponent>())
            {
                if (ImGui::MenuItem("球碰撞器"))
                {
                    entity.AddComponent<SphereColliderComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!entity.HasComponent<ParticleEmitterComponent>())
            {
                if (ImGui::MenuItem("粒子发射器"))
                {
                    entity.AddComponent<ParticleEmitterComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!entity.HasComponent<CollisionParticleTriggerComponent>())
            {
                if (ImGui::MenuItem("碰撞粒子触发器"))
                {
                    entity.AddComponent<CollisionParticleTriggerComponent>();
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
            ImGui::Text("PBR 材质");
            ImGui::DragFloat("金属度", &component.Metallic, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::DragFloat("粗糙度", &component.Roughness, 0.01f, 0.0f, 1.0f, "%.2f");

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

            ImGui::Separator();
            ImGui::Text("法线贴图");
            char normalPathBuf[256];
            memset(normalPathBuf, 0, sizeof(normalPathBuf));
            std::strncpy(normalPathBuf, component.NormalMapPath.c_str(), sizeof(normalPathBuf) - 1);
            if (ImGui::InputText("法线贴图路径", normalPathBuf, sizeof(normalPathBuf)))
            {
                component.NormalMapPath = std::string(normalPathBuf);
            }
            ImGui::SameLine();
            if (ImGui::Button("浏览##NormalMap"))
            {
                std::string absPath = FileDialogs::OpenFile("*.png;*.jpg;*.jpeg;*.bmp;*.tga", "法线贴图");
                if (!absPath.empty())
                {
                    std::error_code ec;
                    std::filesystem::path relative = std::filesystem::relative(absPath, std::filesystem::current_path(), ec);
                    std::string relStr = ec ? absPath : relative.string();

                    if (relStr.find("..") != std::string::npos)
                    {
                        ENGINE_WARN("法线贴图必须位于项目目录内: {0}", relStr);
                    }
                    else
                    {
                        component.NormalMapPath = relStr;
                        component.NormalMapTexture = Texture2D::Create(component.NormalMapPath);
                    }
                }
            }

            if (ImGui::Button("加载法线贴图"))
            {
                if (!component.NormalMapPath.empty())
                    component.NormalMapTexture = Texture2D::Create(component.NormalMapPath);
            }
            ImGui::SameLine();
            if (ImGui::Button("清除法线贴图"))
            {
                component.NormalMapTexture.reset();
                component.NormalMapPath.clear();
            }
        });

        // RigidBody
        DrawComponent<RigidBodyComponent>("刚体", entity, [](auto& component)
        {
            const char* bodyTypeStrings[] = {"静态", "动态", "运动学"};
            const char* currentBodyTypeString = bodyTypeStrings[static_cast<int>(component.Type)];

            if (ImGui::BeginCombo("类型", currentBodyTypeString))
            {
                for (int i = 0; i < 3; i++)
                {
                    bool isSelected = (static_cast<int>(component.Type) == i);
                    if (ImGui::Selectable(bodyTypeStrings[i], isSelected))
                        component.Type = static_cast<RigidBodyComponent::BodyType>(i);
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (component.Type == RigidBodyComponent::BodyType::Dynamic)
            {
                ImGui::DragFloat("质量", &component.Mass, 0.1f, 0.01f, 1000.0f, "%.2f");
            }

            ImGui::DragFloat("弹性系数", &component.Restitution, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::DragFloat("摩擦系数", &component.Friction, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::DragFloat("重力缩放", &component.GravityScale, 0.1f, -10.0f, 10.0f, "%.1f");
            ImGui::Checkbox("固定旋转", &component.FixedRotation);
        });

        // BoxCollider
        DrawComponent<BoxColliderComponent>("盒碰撞器", entity, [this](auto& component)
        {
            DrawVec3Control("半尺寸", component.HalfExtents, 0.5f);
            DrawVec3Control("偏移", component.Offset);
        });

        // SphereCollider
        DrawComponent<SphereColliderComponent>("球碰撞器", entity, [this](auto& component)
        {
            ImGui::DragFloat("半径", &component.Radius, 0.01f, 0.01f, 100.0f, "%.2f");
            DrawVec3Control("偏移", component.Offset);
        });

        // ParticleEmitter
        DrawComponent<ParticleEmitterComponent>("粒子发射器", entity, [this](auto& component)
        {
            // ---- 预设选择 ----
            const char* presetNames[] = {"自定义", "火焰", "烟雾", "爆炸", "火花"};
            int presetIdx = static_cast<int>(component.CurrentPreset);
            if (ImGui::Combo("预设", &presetIdx, presetNames, 5))
            {
                auto preset = static_cast<ParticleEmitterComponent::Preset>(presetIdx);
                if (preset != ParticleEmitterComponent::Preset::Custom)
                {
                    ParticleEmitterComponent::ApplyPreset(component, preset);
                }
                else
                    component.CurrentPreset = ParticleEmitterComponent::Preset::Custom;
            }

            ImGui::Separator();
            ImGui::Text("发射");

            // 检测参数修改 → 自动切换为自定义预设
            bool changed = false;

            changed |= ImGui::DragFloat("发射速率", &component.EmitRate, 1.0f, 0.0f, 100000.0f, "%.0f");
            ImGui::SameLine();
            ImGui::TextDisabled("粒子/秒");
            changed |= ImGui::DragInt("爆发数量", &component.BurstCount, 1, 0, 10000);
            ImGui::SameLine();
            if (ImGui::Button("触发爆发"))
                component.PendingBurst = component.BurstCount;

            int maxP = static_cast<int>(component.MaxParticles);
            if (ImGui::DragInt("最大粒子数", &maxP, 100, 100, 1000000))
            {
                component.MaxParticles = static_cast<uint32_t>(std::max(maxP, 100));
                changed = true;
            }

            ImGui::Separator();
            ImGui::Text("生命周期");
            changed |= ImGui::DragFloat("最短寿命", &component.LifeMin, 0.05f, 0.01f, 60.0f, "%.2f 秒");
            changed |= ImGui::DragFloat("最长寿命", &component.LifeMax, 0.05f, 0.01f, 60.0f, "%.2f 秒");
            if (component.LifeMax < component.LifeMin)
                component.LifeMax = component.LifeMin;

            ImGui::Separator();
            ImGui::Text("速度与方向");
            changed |= ImGui::DragFloat("最小速度", &component.SpeedMin, 0.1f, 0.0f, 100.0f, "%.1f");
            changed |= ImGui::DragFloat("最大速度", &component.SpeedMax, 0.1f, 0.0f, 100.0f, "%.1f");
            if (component.SpeedMax < component.SpeedMin)
                component.SpeedMax = component.SpeedMin;
            DrawVec3Control("发射方向", component.EmitDirection);
            changed |= ImGui::DragFloat("锥角", &component.EmitAngle, 0.5f, 0.0f, 180.0f, "%.1f°");

            ImGui::Separator();
            ImGui::Text("大小");
            changed |= ImGui::DragFloat("起始大小", &component.SizeStart, 0.01f, 0.001f, 10.0f, "%.3f");
            changed |= ImGui::DragFloat("结束大小", &component.SizeEnd, 0.01f, 0.0f, 10.0f, "%.3f");

            ImGui::Separator();
            ImGui::Text("颜色");
            changed |= ImGui::ColorEdit4("起始颜色", glm::value_ptr(component.ColorStart));
            changed |= ImGui::ColorEdit4("结束颜色", glm::value_ptr(component.ColorEnd));

            ImGui::Separator();
            ImGui::Text("物理");
            DrawVec3Control("重力", component.Gravity);
            changed |= ImGui::DragFloat("阻尼", &component.Damping, 0.01f, 0.0f, 1.0f, "%.2f");

            ImGui::Separator();
            const char* blendModes[] = {"加法混合", "Alpha混合"};
            int blendIdx = static_cast<int>(component.Blend);
            if (ImGui::Combo("混合模式", &blendIdx, blendModes, 2))
            {
                component.Blend = static_cast<ParticleEmitterComponent::BlendMode>(blendIdx);
                changed = true;
            }

            // 参数修改后自动切换为自定义
            if (changed && component.CurrentPreset != ParticleEmitterComponent::Preset::Custom)
                component.CurrentPreset = ParticleEmitterComponent::Preset::Custom;

            // ---- SPH 流体参数 ----
            ImGui::Separator();
            ImGui::Text("SPH 流体");
            ImGui::Checkbox("启用 SPH", &component.SPHEnabled);
            if (component.SPHEnabled)
            {
                ImGui::DragFloat("静止密度", &component.SPH_RestDensity, 10.0f, 100.0f, 10000.0f, "%.0f");
                ImGui::DragFloat("气体常数", &component.SPH_GasConstant, 10.0f, 100.0f, 50000.0f, "%.0f");
                ImGui::DragFloat("粘性系数", &component.SPH_Viscosity, 0.001f, 0.0f, 1.0f, "%.4f");
                ImGui::DragFloat("光滑半径", &component.SPH_SmoothingRadius, 0.01f, 0.01f, 2.0f, "%.3f");
                ImGui::DragFloat("粒子质量", &component.SPH_ParticleMass, 0.001f, 0.001f, 1.0f, "%.4f");

                ImGui::Separator();
                ImGui::Text("PCISPH");
                ImGui::Checkbox("启用 PCISPH", &component.SPH_PCISPHEnabled);
                if (component.SPH_PCISPHEnabled)
                {
                    ImGui::SliderInt("PCISPH 迭代次数", &component.SPH_PCISPHIterations, 1, 8);
                    ImGui::DragFloat("PCISPH 校正系数", &component.SPH_PCISPHDelta, 0.01f, 0.01f, 1.0f, "%.3f");
                }

                ImGui::Separator();
                ImGui::Text("表面张力");
                ImGui::DragFloat("表面张力系数", &component.SPH_SurfaceTension, 0.1f, 0.0f, 20.0f, "%.2f");

                ImGui::Separator();
                ImGui::Text("刚体耦合");
                ImGui::Checkbox("启用刚体耦合", &component.SPH_RigidBodyCoupling);
                if (component.SPH_RigidBodyCoupling)
                {
                    ImGui::DragFloat("边界刚度", &component.SPH_BoundaryStiffness, 100.0f, 100.0f, 50000.0f, "%.0f");
                    ImGui::DragFloat("边界阻尼", &component.SPH_BoundaryDamping, 0.01f, 0.0f, 1.0f, "%.2f");
                }
            }
        });

        // CollisionParticleTrigger
        DrawComponent<CollisionParticleTriggerComponent>("碰撞粒子触发器", entity, [](auto& component)
        {
            ImGui::Checkbox("启用", &component.Enabled);
            ImGui::DragInt("爆发粒子数", &component.BurstOnCollision, 1, 1, 1000);
            ImGui::DragFloat("最小冲量", &component.MinImpulse, 0.1f, 0.0f, 100.0f, "%.1f");
            ImGui::Checkbox("使用碰撞法线", &component.UseCollisionNormal);
        });
    }

} // namespace Engine
