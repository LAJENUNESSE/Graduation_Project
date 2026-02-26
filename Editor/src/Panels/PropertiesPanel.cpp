#include "Panels/PropertiesPanel.h"
#include "Scene/SceneCamera.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Asset/AssetManager.h"
#include "Core/FileDialogs.h"
#include "Core/Log.h"
#include "Reflection/ComponentRegistry.h"
#include "Reflection/AutoInspector.h"
#include "Script/NativeScriptComponent.h"
#include "Script/ScriptRegistry.h"

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

    void PropertiesPanel::DrawComponent_Auto(const std::string& name, Entity entity,
                                              const ComponentMeta& meta, AutoInspector::DrawVec3Fn drawVec3)
    {
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                                                 ImGuiTreeNodeFlags_SpanAvailWidth |
                                                 ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;

        auto* scene = entity.GetScene();
        uint32_t entityId = static_cast<uint32_t>(static_cast<entt::entity>(entity));
        if (!scene || !meta.Has(*scene, entityId))
            return;

        void* component = meta.Get(*scene, entityId);
        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4, 4});
        float lineHeight = ImGui::GetFrameHeight();
        ImGui::Separator();
        bool open = ImGui::TreeNodeEx(meta.TypeName, treeNodeFlags, "%s", name.c_str());
        ImGui::PopStyleVar();

        bool removeComponent = false;
        ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
        if (ImGui::Button("+", ImVec2{lineHeight, lineHeight}))
            ImGui::OpenPopup("ComponentSettings");

        if (ImGui::BeginPopup("ComponentSettings"))
        {
            if (ImGui::MenuItem("Remove Component"))
                removeComponent = true;
            ImGui::EndPopup();
        }

        if (open)
        {
            AutoInspector::Draw(meta, component, drawVec3);
            ImGui::TreePop();
        }

        if (removeComponent)
            meta.Remove(*scene, entityId);
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

            // 反射组件自动添加菜单
            {
                auto* scene = entity.GetScene();
                uint32_t entityId = static_cast<uint32_t>(static_cast<entt::entity>(entity));
                for (auto& meta : ComponentRegistry::Instance().GetAll())
                {
                    if (scene && !meta.Has(*scene, entityId))
                    {
                        if (ImGui::MenuItem(meta.DisplayName))
                        {
                            meta.Add(*scene, entityId);
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
            }

            if (!entity.HasComponent<TerrainComponent>())
            {
                if (ImGui::MenuItem("地形"))
                {
                    entity.AddComponent<TerrainComponent>();
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

            if (!entity.HasComponent<NativeScriptComponent>())
            {
                if (ImGui::MenuItem("脚本"))
                {
                    entity.AddComponent<NativeScriptComponent>();
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

        // Light — 通过反射自动绘制（见下方统一循环）
        // RigidBody — 通过反射自动绘制
        // BoxCollider — 通过反射自动绘制
        // SphereCollider — 通过反射自动绘制
        // CollisionParticleTrigger — 通过反射自动绘制

        // Mesh Renderer
        DrawComponent<MeshRendererComponent>("网格渲染器", entity, [](auto& component)
        {
            ImGui::ColorEdit4("颜色", glm::value_ptr(component.Color));

            // Mesh primitive selection
            const char* meshTypeLabels[] = {"Cube", "Plane", "Sphere", "Model"};
            int currentIdx = static_cast<int>(component.Type);
            if (currentIdx > 3) currentIdx = 0;

            if (ImGui::BeginCombo("网格", meshTypeLabels[currentIdx]))
            {
                if (ImGui::Selectable("Cube", component.Type == MeshType::Cube))
                {
                    component.Type = MeshType::Cube;
                    component.MeshAsset = AssetManager::Load<Mesh>("builtin:Cube");
                }
                if (ImGui::Selectable("Plane", component.Type == MeshType::Plane))
                {
                    component.Type = MeshType::Plane;
                    component.MeshAsset = AssetManager::Load<Mesh>("builtin:Plane");
                }
                if (ImGui::Selectable("Sphere", component.Type == MeshType::Sphere))
                {
                    component.Type = MeshType::Sphere;
                    component.MeshAsset = AssetManager::Load<Mesh>("builtin:Sphere");
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
                        auto meshHandle = AssetManager::Load<Mesh>(relStr);
                        if (meshHandle.IsValid())
                        {
                            component.Type = MeshType::Model;
                            component.MeshAsset = meshHandle;

                            // Auto-detect: if model has submesh textures, clear the component-level texture
                            Mesh* mesh = AssetManager::Get<Mesh>(meshHandle);
                            if (mesh)
                            {
                                bool hasSubMeshTex = false;
                                for (const auto& sub : mesh->GetSubMeshes())
                                {
                                    if (sub.DiffuseTextureAsset.IsValid())
                                    {
                                        hasSubMeshTex = true;
                                        break;
                                    }
                                }
                                if (hasSubMeshTex)
                                    component.DiffuseTextureAsset = {};
                            }
                        }
                    }
                }
            }

            // Show model path if loaded
            if (component.Type == MeshType::Model && component.MeshAsset.IsValid())
            {
                ImGui::SameLine();
                const std::string& modelPath = AssetManager::GetPath<Mesh>(component.MeshAsset);
                ImGui::TextDisabled("(%s)", modelPath.c_str());
            }

            ImGui::Separator();
            ImGui::Text("PBR 材质");
            ImGui::DragFloat("金属度", &component.Metallic, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::DragFloat("粗糙度", &component.Roughness, 0.01f, 0.0f, 1.0f, "%.2f");

            // Texture path display + edit
            {
                const std::string& texPath = AssetManager::GetPath<Texture2D>(component.DiffuseTextureAsset);
                char texPathBuf[256];
                memset(texPathBuf, 0, sizeof(texPathBuf));
                std::strncpy(texPathBuf, texPath.c_str(), sizeof(texPathBuf) - 1);
                if (ImGui::InputText("纹理路径", texPathBuf, sizeof(texPathBuf), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    std::string newPath(texPathBuf);
                    if (!newPath.empty())
                        component.DiffuseTextureAsset = AssetManager::Load<Texture2D>(newPath);
                    else
                        component.DiffuseTextureAsset = {};
                }
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

                    if (relStr.find("..") != std::string::npos)
                    {
                        ENGINE_WARN("纹理必须位于项目目录内: {0}", relStr);
                    }
                    else
                    {
                        component.DiffuseTextureAsset = AssetManager::Load<Texture2D>(relStr);
                    }
                }
            }

            if (ImGui::Button("加载纹理"))
            {
                const std::string& texPath = AssetManager::GetPath<Texture2D>(component.DiffuseTextureAsset);
                if (!texPath.empty())
                    component.DiffuseTextureAsset = AssetManager::Load<Texture2D>(texPath);
            }
            ImGui::SameLine();
            if (ImGui::Button("清除纹理"))
            {
                component.DiffuseTextureAsset = {};
            }

            ImGui::DragFloat("平铺 X", &component.Tiling.x, 0.1f, 0.01f, 100.0f, "%.2f");
            ImGui::DragFloat("平铺 Y", &component.Tiling.y, 0.1f, 0.01f, 100.0f, "%.2f");

            ImGui::Separator();
            ImGui::Text("法线贴图");
            {
                const std::string& normalPath = AssetManager::GetPath<Texture2D>(component.NormalMapAsset);
                char normalPathBuf[256];
                memset(normalPathBuf, 0, sizeof(normalPathBuf));
                std::strncpy(normalPathBuf, normalPath.c_str(), sizeof(normalPathBuf) - 1);
                if (ImGui::InputText("法线贴图路径", normalPathBuf, sizeof(normalPathBuf), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    std::string newPath(normalPathBuf);
                    if (!newPath.empty())
                        component.NormalMapAsset = AssetManager::Load<Texture2D>(newPath);
                    else
                        component.NormalMapAsset = {};
                }
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
                        component.NormalMapAsset = AssetManager::Load<Texture2D>(relStr);
                    }
                }
            }

            if (ImGui::Button("加载法线贴图"))
            {
                const std::string& normalPath = AssetManager::GetPath<Texture2D>(component.NormalMapAsset);
                if (!normalPath.empty())
                    component.NormalMapAsset = AssetManager::Load<Texture2D>(normalPath);
            }
            ImGui::SameLine();
            if (ImGui::Button("清除法线贴图"))
            {
                component.NormalMapAsset = {};
            }
        });

        // RigidBody — 通过反射自动绘制（见下方统一循环）

        // BoxCollider — 通过反射自动绘制

        // SphereCollider — 通过反射自动绘制

        // Terrain
        DrawComponent<TerrainComponent>("地形", entity, [this](auto& component)
        {
            // ---- 高度图 ----
            ImGui::Text("高度图");
            {
                char buf[256];
                memset(buf, 0, sizeof(buf));
                std::strncpy(buf, component.HeightmapPath.c_str(), sizeof(buf) - 1);
                if (ImGui::InputText("高度图路径", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    component.HeightmapPath = std::string(buf);
                    component.MeshDirty = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("浏览##Heightmap"))
            {
                std::string absPath = FileDialogs::OpenFile("*.png;*.jpg;*.bmp;*.tga", "高度图");
                if (!absPath.empty())
                {
                    std::error_code ec;
                    auto relative = std::filesystem::relative(absPath, std::filesystem::current_path(), ec);
                    std::string relStr = ec ? absPath : relative.string();
                    if (relStr.find("..") == std::string::npos)
                    {
                        component.HeightmapPath = relStr;
                        component.MeshDirty = true;
                    }
                }
            }

            if (ImGui::Button("重新生成网格"))
                component.MeshDirty = true;

            if (ImGui::DragFloat("高度缩放", &component.HeightScale, 0.5f, 0.1f, 500.0f, "%.1f"))
                component.MeshDirty = true;
            if (ImGui::DragFloat("地形尺寸", &component.TerrainSize, 1.0f, 1.0f, 1000.0f, "%.1f"))
                component.MeshDirty = true;

            ImGui::Separator();

            // ---- Splatmap ----
            ImGui::Text("Splat Map");
            {
                char buf[256];
                memset(buf, 0, sizeof(buf));
                std::strncpy(buf, component.SplatmapPath.c_str(), sizeof(buf) - 1);
                if (ImGui::InputText("Splatmap 路径", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
                    component.SplatmapPath = std::string(buf);
            }
            ImGui::SameLine();
            if (ImGui::Button("浏览##Splatmap"))
            {
                std::string absPath = FileDialogs::OpenFile("*.png;*.jpg;*.bmp;*.tga", "Splatmap");
                if (!absPath.empty())
                {
                    std::error_code ec;
                    auto relative = std::filesystem::relative(absPath, std::filesystem::current_path(), ec);
                    std::string relStr = ec ? absPath : relative.string();
                    if (relStr.find("..") == std::string::npos)
                        component.SplatmapPath = relStr;
                }
            }

            ImGui::Separator();

            // ---- 4 层纹理 ----
            const char* layerNames[] = {"层0 (草地)", "层1 (泥土)", "层2 (岩石)", "层3 (雪地)"};
            for (int i = 0; i < 4; i++)
            {
                ImGui::PushID(i);
                if (ImGui::TreeNode(layerNames[i]))
                {
                    // Albedo 贴图路径 + 浏览按钮
                    const std::string& texPath = AssetManager::GetPath<Texture2D>(component.LayerTextures[i]);
                    char texBuf[256];
                    memset(texBuf, 0, sizeof(texBuf));
                    std::strncpy(texBuf, texPath.c_str(), sizeof(texBuf) - 1);
                    if (ImGui::InputText("反照率", texBuf, sizeof(texBuf), ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        std::string p(texBuf);
                        component.LayerTextures[i] = p.empty() ? AssetHandle{} : AssetManager::Load<Texture2D>(p);
                    }
                    ImGui::SameLine();
                    std::string browseId = "浏览##LayerTex" + std::to_string(i);
                    if (ImGui::Button(browseId.c_str()))
                    {
                        std::string absPath = FileDialogs::OpenFile("*.png;*.jpg;*.bmp;*.tga", "贴图");
                        if (!absPath.empty())
                        {
                            std::error_code ec;
                            auto rel = std::filesystem::relative(absPath, std::filesystem::current_path(), ec);
                            std::string relStr = ec ? absPath : rel.string();
                            if (relStr.find("..") == std::string::npos)
                                component.LayerTextures[i] = AssetManager::Load<Texture2D>(relStr);
                        }
                    }

                    // 法线贴图
                    const std::string& normPath = AssetManager::GetPath<Texture2D>(component.LayerNormalMaps[i]);
                    char normBuf[256];
                    memset(normBuf, 0, sizeof(normBuf));
                    std::strncpy(normBuf, normPath.c_str(), sizeof(normBuf) - 1);
                    if (ImGui::InputText("法线", normBuf, sizeof(normBuf), ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        std::string p(normBuf);
                        component.LayerNormalMaps[i] = p.empty() ? AssetHandle{} : AssetManager::Load<Texture2D>(p);
                    }

                    ImGui::DragFloat("平铺", &component.LayerTiling[i], 0.5f, 0.1f, 100.0f, "%.1f");
                    ImGui::DragFloat("金属度", &component.LayerMetallic[i], 0.01f, 0.0f, 1.0f, "%.2f");
                    ImGui::DragFloat("粗糙度", &component.LayerRoughness[i], 0.01f, 0.0f, 1.0f, "%.2f");

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            ImGui::Separator();

            // ---- 物理参数 ----
            ImGui::Text("物理");
            ImGui::DragFloat("摩擦力", &component.Friction, 0.01f, 0.0f, 2.0f, "%.2f");
            ImGui::DragFloat("弹性", &component.Restitution, 0.01f, 0.0f, 1.0f, "%.2f");

            ImGui::Separator();

            // ---- LOD ----
            ImGui::Text("细节层次 (LOD)");
            if (ImGui::SliderInt("LOD 层数", &component.LODLevels, 1, 3))
                component.MeshDirty = true;
            ImGui::DragFloat("LOD1 距离", &component.LODDistance1, 1.0f, 10.0f, 500.0f, "%.0f");
            ImGui::DragFloat("LOD2 距离", &component.LODDistance2, 1.0f, 20.0f, 1000.0f, "%.0f");

            ImGui::Separator();

            // ---- 草地 ----
            ImGui::Text("草地");
            ImGui::Checkbox("启用草地", &component.GrassEnabled);
            if (component.GrassEnabled)
            {
                ImGui::DragFloat("草密度", &component.GrassDensity, 0.5f, 0.1f, 50.0f, "%.1f 片/m²");
                ImGui::DragFloat("草高度", &component.GrassHeight, 0.01f, 0.01f, 2.0f, "%.2f");
                ImGui::DragFloat("草宽度", &component.GrassWidth, 0.01f, 0.01f, 1.0f, "%.2f");
                ImGui::DragFloat("风力", &component.GrassWindStrength, 0.01f, 0.0f, 2.0f, "%.2f");
                // 草地贴图
                const std::string& grassPath = AssetManager::GetPath<Texture2D>(component.GrassTexture);
                char grassBuf[256];
                memset(grassBuf, 0, sizeof(grassBuf));
                std::strncpy(grassBuf, grassPath.c_str(), sizeof(grassBuf) - 1);
                if (ImGui::InputText("草贴图", grassBuf, sizeof(grassBuf), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    std::string p(grassBuf);
                    component.GrassTexture = p.empty() ? AssetHandle{} : AssetManager::Load<Texture2D>(p);
                }
            }
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

        // CollisionParticleTrigger — 通过反射自动绘制

        // ---- 反射组件统一绘制 ----
        {
            // DrawVec3Control 适配函数（包装成 AutoInspector 需要的签名）
            static PropertiesPanel* s_Panel = nullptr;
            s_Panel = this;
            auto drawVec3Wrapper = [](const char* label, float* values, float resetValue) {
                s_Panel->DrawVec3Control(label, *reinterpret_cast<glm::vec3*>(values), resetValue);
            };

            auto* scene = entity.GetScene();
            uint32_t entityId = static_cast<uint32_t>(static_cast<entt::entity>(entity));

            for (auto& meta : ComponentRegistry::Instance().GetAll())
            {
                if (scene && meta.Has(*scene, entityId))
                {
                    DrawComponent_Auto(meta.DisplayName, entity, meta, drawVec3Wrapper);
                }
            }
        }

        // ---- NativeScript 组件 ----
        DrawComponent<NativeScriptComponent>("脚本", entity, [](auto& component)
        {
            auto& scripts = ScriptRegistry::Instance().GetAll();
            const char* currentName = component.ScriptName.empty() ? "(无)" : component.ScriptName.c_str();

            // 查找当前脚本的显示名
            for (auto& [name, entry] : scripts)
            {
                if (name == component.ScriptName)
                {
                    currentName = entry.DisplayName;
                    break;
                }
            }

            if (ImGui::BeginCombo("脚本类", currentName))
            {
                // 空选项
                if (ImGui::Selectable("(无)", component.ScriptName.empty()))
                {
                    component.ScriptName.clear();
                    component.InstantiateScript = nullptr;
                    component.DestroyScript = nullptr;
                    if (component.Instance)
                    {
                        component.Instance.reset();
                    }
                }

                for (auto& [name, entry] : scripts)
                {
                    bool selected = (name == component.ScriptName);
                    if (ImGui::Selectable(entry.DisplayName, selected))
                    {
                        component.Instance.reset();
                        ScriptRegistry::Instance().Bind(component, name);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        });
    }

} // namespace Engine
