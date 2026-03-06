#include "Panels/PropertiesPanel.h"
#include "Panels/PropertiesPanelCustomDrawers.h"
#include "Scene/SceneCamera.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Asset/AssetManager.h"
#include "Asset/PathUtils.h"
#include "Core/FileDialogs.h"
#include "Core/Log.h"
#include "Reflection/ComponentRegistry.h"
#include "Reflection/ComponentPolicies.h"
#include "Reflection/AutoInspector.h"
#include "Script/NativeScriptComponent.h"
#include "Script/ScriptRegistry.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>

#include <filesystem>

namespace Engine
{
    namespace
    {
        bool TrySelectProjectAssetPath(const char* filter, const char* description, const char* assetLabel, std::string& outPath)
        {
            std::string selectedPath = FileDialogs::OpenFile(filter, description);
            if (selectedPath.empty())
                return false;

            if (PathUtils::TryToProjectRelative(selectedPath, outPath))
                return true;

            ENGINE_WARN("{}必须位于项目目录内: {}", assetLabel, selectedPath);
            return false;
        }
    } // namespace


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
                    if (scene && !meta.Has(*scene, entityId) &&
                        !ComponentPolicies::IsCustomAddMenuComponentType(meta.TypeName))
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

            if (!entity.HasComponent<AudioSourceComponent>())
            {
                if (ImGui::MenuItem("音频源"))
                {
                    entity.AddComponent<AudioSourceComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!entity.HasComponent<AudioListenerComponent>())
            {
                if (ImGui::MenuItem("音频监听器"))
                {
                    entity.AddComponent<AudioListenerComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!entity.HasComponent<VideoPlayerComponent>())
            {
                if (ImGui::MenuItem("视频播放器"))
                {
                    entity.AddComponent<VideoPlayerComponent>();
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
        DrawComponent<MeshRendererComponent>("\u7f51\u683c\u6e32\u67d3\u5668", entity, [](auto& component)
        {
            PropertiesPanelCustomDrawers::DrawMeshRendererInspector(component);
        });
        // RigidBody — 通过反射自动绘制（见下方统一循环）

        // BoxCollider — 通过反射自动绘制

        // SphereCollider — 通过反射自动绘制

        // Terrain
        DrawComponent<TerrainComponent>("\u5730\u5f62", entity, [this](auto& component)
        {
            PropertiesPanelCustomDrawers::DrawTerrainInspector(component);
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

        // AudioSource
        DrawComponent<AudioSourceComponent>("\u97f3\u9891\u6e90", entity, [](auto& component)
        {
            PropertiesPanelCustomDrawers::DrawAudioSourceInspector(component);
        });
        // AudioListener
        DrawComponent<AudioListenerComponent>("\u97f3\u9891\u76d1\u542c\u5668", entity, [](auto& component)
        {
            PropertiesPanelCustomDrawers::DrawAudioListenerInspector(component);
        });
        // VideoPlayer
        DrawComponent<VideoPlayerComponent>("\u89c6\u9891\u64ad\u653e\u5668", entity, [](auto& component)
        {
            PropertiesPanelCustomDrawers::DrawVideoPlayerInspector(component);
        });
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
                if (scene && meta.Has(*scene, entityId) &&
                    !ComponentPolicies::IsCustomInspectorComponentType(meta.TypeName))
                {
                    DrawComponent_Auto(meta.DisplayName, entity, meta, drawVec3Wrapper);
                }
            }
        }

        // ---- NativeScript 组件 ----
        DrawComponent<NativeScriptComponent>("\u811a\u672c", entity, [](auto& component)
        {
            PropertiesPanelCustomDrawers::DrawNativeScriptInspector(component);
        });
    }

} // namespace Engine
