#include "Panels/PropertiesPanel.h"
#include "Asset/AssetManager.h"
#include "Asset/PathUtils.h"
#include "Core/FileDialogs.h"
#include "Core/Log.h"
#include "Panels/PropertiesPanelCustomDrawers.h"
#include "Reflection/AutoInspector.h"
#include "Reflection/ComponentRegistry.h"
#include "Renderer/Mesh.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/Texture.h"
#include "Scene/SceneCamera.h"
#include "Script/NativeScriptComponent.h"
#include "Script/ScriptRegistry.h"
#include "UndoSystem.h"

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <filesystem>

namespace Engine
{
    namespace
    {
        constexpr float kVec3ControlEpsilon = 0.0001f;

        bool AreVec3Equal(const glm::vec3& lhs, const glm::vec3& rhs)
        {
            return std::abs(lhs.x - rhs.x) <= kVec3ControlEpsilon && std::abs(lhs.y - rhs.y) <= kVec3ControlEpsilon &&
                   std::abs(lhs.z - rhs.z) <= kVec3ControlEpsilon;
        }

        bool TrySelectProjectAssetPath(const char*  filter,
                                       const char*  description,
                                       const char*  assetLabel,
                                       std::string& outPath)
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

    void PropertiesPanel::SetReadOnly(bool readOnly)
    {
        m_ReadOnly = readOnly;
        if (readOnly)
            m_TransformEditSession = {};
    }
    template <typename T, typename UIFunction>
    void PropertiesPanel::DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction, bool removable)
    {
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                                                 ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
                                                 ImGuiTreeNodeFlags_FramePadding;

        if (!entity.HasComponent<T>())
            return;

        auto&     component   = entity.GetComponent<T>();
        const int componentID = static_cast<int>(typeid(T).hash_code());
        ImGui::PushID(componentID);

        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4, 4});
        float lineHeight = ImGui::GetFrameHeight();
        ImGui::Separator();
        bool open = ImGui::TreeNodeEx("ComponentHeader", treeNodeFlags, "%s", name.c_str());
        ImGui::PopStyleVar();

        bool removeComponent = false;
        if (removable)
        {
            ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
            if (ImGui::Button("+##ComponentMenu", ImVec2{lineHeight, lineHeight}))
                ImGui::OpenPopup("ComponentSettings");

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
        {
            entity.RemoveComponent<T>();
            m_FrameModified = true;
        }

        ImGui::PopID();
    }

    void PropertiesPanel::DrawComponent_Auto(const std::string&        name,
                                             Entity                    entity,
                                             const ComponentMeta&      meta,
                                             AutoInspector::DrawVec3Fn drawVec3)
    {
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                                                 ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
                                                 ImGuiTreeNodeFlags_FramePadding;

        auto*    scene    = entity.GetScene();
        uint32_t entityId = static_cast<uint32_t>(static_cast<entt::entity>(entity));
        if (!scene || !meta.Has(*scene, entityId))
            return;

        void* component = meta.Get(*scene, entityId);
        ImGui::PushID(meta.TypeName);
        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4, 4});
        float lineHeight = ImGui::GetFrameHeight();
        ImGui::Separator();
        bool open = ImGui::TreeNodeEx("ComponentHeader", treeNodeFlags, "%s", name.c_str());
        ImGui::PopStyleVar();

        bool removeComponent = false;
        ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
        if (ImGui::Button("+##ComponentMenu", ImVec2{lineHeight, lineHeight}))
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
        {
            meta.Remove(*scene, entityId);
            m_FrameModified = true;
        }

        ImGui::PopID();
    }

    PropertiesPanel::Vec3ControlEditState
    PropertiesPanel::DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue, float columnWidth)
    {
        ImGuiIO&             io       = ImGui::GetIO();
        auto                 boldFont = io.Fonts->Fonts[0];
        Vec3ControlEditState state;

        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::Text("%s", label.c_str());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 0});

        float  lineHeight = ImGui::GetFrameHeight();
        ImVec2 buttonSize = {lineHeight + 3.0f, lineHeight};

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.9f, 0.2f, 0.2f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
        ImGui::PushFont(boldFont);
        if (ImGui::Button("X", buttonSize))
        {
            values.x           = resetValue;
            state.ValueChanged = true;
            state.EditStarted  = true;
            state.EditFinished = true;
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        state.ValueChanged = ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f") || state.ValueChanged;
        state.EditStarted  = ImGui::IsItemActivated() || state.EditStarted;
        state.EditFinished = ImGui::IsItemDeactivatedAfterEdit() || state.EditFinished;
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.2f, 0.7f, 0.2f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.3f, 0.8f, 0.3f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.2f, 0.7f, 0.2f, 1.0f});
        ImGui::PushFont(boldFont);
        if (ImGui::Button("Y", buttonSize))
        {
            values.y           = resetValue;
            state.ValueChanged = true;
            state.EditStarted  = true;
            state.EditFinished = true;
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        state.ValueChanged = ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f") || state.ValueChanged;
        state.EditStarted  = ImGui::IsItemActivated() || state.EditStarted;
        state.EditFinished = ImGui::IsItemDeactivatedAfterEdit() || state.EditFinished;
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.35f, 0.9f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
        ImGui::PushFont(boldFont);
        if (ImGui::Button("Z", buttonSize))
        {
            values.z           = resetValue;
            state.ValueChanged = true;
            state.EditStarted  = true;
            state.EditFinished = true;
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        state.ValueChanged = ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f") || state.ValueChanged;
        state.EditStarted  = ImGui::IsItemActivated() || state.EditStarted;
        state.EditFinished = ImGui::IsItemDeactivatedAfterEdit() || state.EditFinished;
        ImGui::PopItemWidth();

        ImGui::PopStyleVar();
        ImGui::Columns(1);

        ImGui::PopID();
        return state;
    }
    void PropertiesPanel::OnImGuiRender(Entity selectedEntity)
    {
        ImGui::Begin("属性");
        m_FrameModified = false;

        const bool hasValidSelection =
            selectedEntity && selectedEntity.GetScene() &&
            selectedEntity.GetScene()->GetRegistry().valid(static_cast<entt::entity>(selectedEntity));

        if (hasValidSelection)
        {
            if (m_TransformEditSession.Active && (static_cast<uint64_t>(m_TransformEditSession.EntityID) !=
                                                      static_cast<uint64_t>(selectedEntity.GetUUID()) ||
                                                  m_TransformEditSession.SceneContext != selectedEntity.GetScene()))
            {
                m_TransformEditSession = {};
            }

            if (m_ReadOnly)
                ImGui::BeginDisabled();
            DrawComponents(selectedEntity);
            if (m_ReadOnly)
                ImGui::EndDisabled();
        }
        else
        {
            m_TransformEditSession = {};
        }

        if (m_FrameModified && m_SceneModifiedCallback)
            m_SceneModifiedCallback();

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
                tag             = std::string(buffer);
                m_FrameModified = true;
            }
        }
        ImGui::SameLine();
        ImGui::PushItemWidth(-1);

        if (ImGui::Button("添加组件"))
            ImGui::OpenPopup("AddComponent");

        if (ImGui::BeginPopup("AddComponent"))
        {
            // Camera 需要特殊初始化（默认参数设置），保留手写
            if (!entity.HasComponent<CameraComponent>())
            {
                if (ImGui::MenuItem("相机"))
                {
                    entity.AddComponent<CameraComponent>();
                    m_FrameModified = true;
                    ImGui::CloseCurrentPopup();
                }
            }

            // NativeScript 无法通过反射初始化（需要 ScriptRegistry 绑定），保留手写
            if (!entity.HasComponent<NativeScriptComponent>())
            {
                if (ImGui::MenuItem("脚本"))
                {
                    entity.AddComponent<NativeScriptComponent>();
                    m_FrameModified = true;
                    ImGui::CloseCurrentPopup();
                }
            }

            // 所有 ComponentRegistry 注册的组件统一添加菜单
            {
                auto*    scene    = entity.GetScene();
                uint32_t entityId = static_cast<uint32_t>(static_cast<entt::entity>(entity));
                for (auto& meta : ComponentRegistry::Instance().GetAll())
                {
                    // 跳过 Camera 和 NativeScript（上方已手写添加）
                    if (std::string_view(meta.TypeName) == "CameraComponent" ||
                        std::string_view(meta.TypeName) == "NativeScriptComponent")
                        continue;

                    if (scene && !meta.Has(*scene, entityId))
                    {
                        if (ImGui::MenuItem(meta.DisplayName))
                        {
                            meta.Add(*scene, entityId);
                            m_FrameModified = true;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
            }

            ImGui::EndPopup();
        }
        ImGui::PopItemWidth();

        // Transform
        const entt::entity entityHandle  = static_cast<entt::entity>(entity);
        const uint64_t     entityIDValue = static_cast<uint64_t>(entity.GetUUID());
        Scene* const       entityScene   = entity.GetScene();

        DrawComponent<TransformComponent>(
            "变换", entity,
            [this, entityHandle, entityIDValue, entityScene](auto& component)
            {
                const glm::vec3 oldTranslation = component.Translation;
                glm::vec3       rotation       = glm::degrees(component.Rotation);
                const glm::vec3 oldRotation    = component.Rotation;
                const glm::vec3 oldScale       = component.Scale;

                const Vec3ControlEditState translationState = DrawVec3Control("位移", component.Translation);
                const Vec3ControlEditState rotationState    = DrawVec3Control("旋转", rotation);
                component.Rotation                          = glm::radians(rotation);
                const Vec3ControlEditState scaleState       = DrawVec3Control("缩放", component.Scale, 1.0f);

                const bool editStarted =
                    translationState.EditStarted || rotationState.EditStarted || scaleState.EditStarted;
                const bool editFinished =
                    translationState.EditFinished || rotationState.EditFinished || scaleState.EditFinished;

                if (editStarted && !m_TransformEditSession.Active)
                {
                    m_TransformEditSession.Active       = true;
                    m_TransformEditSession.EntityID     = UUID(entityIDValue);
                    m_TransformEditSession.SceneContext = entityScene;
                    m_TransformEditSession.Translation  = oldTranslation;
                    m_TransformEditSession.Rotation     = oldRotation;
                    m_TransformEditSession.Scale        = oldScale;
                }

                if (editFinished && m_TransformEditSession.Active)
                {
                    if (m_CommandHistory && static_cast<uint64_t>(m_TransformEditSession.EntityID) == entityIDValue &&
                        m_TransformEditSession.SceneContext == entityScene &&
                        (!AreVec3Equal(m_TransformEditSession.Translation, component.Translation) ||
                         !AreVec3Equal(m_TransformEditSession.Rotation, component.Rotation) ||
                         !AreVec3Equal(m_TransformEditSession.Scale, component.Scale)))
                    {
                        m_CommandHistory->PushExecutedCommand(CreateRef<TransformChangeCommand>(
                            m_ActiveScene, Entity(entityHandle, entityScene), m_TransformEditSession.Translation,
                            m_TransformEditSession.Rotation, m_TransformEditSession.Scale, component.Translation,
                            component.Rotation, component.Scale));
                    }

                    m_TransformEditSession = {};
                }
            },
            false);

        // Camera
        DrawComponent<CameraComponent>(
            "相机", entity,
            [this](auto& component)
            {
                auto& camera = component.Camera;

                m_FrameModified |= ImGui::Checkbox("主相机", &component.Primary);

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
                            m_FrameModified = true;
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
                    {
                        camera.SetPerspectiveVerticalFOV(glm::radians(std::clamp(perspectiveFOV, 1.0f, 179.0f)));
                        m_FrameModified = true;
                    }

                    float perspectiveNear = camera.GetPerspectiveNearClip();
                    if (ImGui::DragFloat("近平面", &perspectiveNear, 0.01f, 0.001f, 0.0f))
                    {
                        perspectiveNear = std::max(perspectiveNear, 0.001f);
                        camera.SetPerspectiveNearClip(perspectiveNear);
                        m_FrameModified = true;
                    }

                    float perspectiveFar = camera.GetPerspectiveFarClip();
                    if (ImGui::DragFloat("远平面", &perspectiveFar, 1.0f, 0.0f, 0.0f))
                    {
                        perspectiveFar = std::max(perspectiveFar, camera.GetPerspectiveNearClip() + 0.1f);
                        camera.SetPerspectiveFarClip(perspectiveFar);
                        m_FrameModified = true;
                    }
                }

                if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
                {
                    float orthoSize = camera.GetOrthographicSize();
                    if (ImGui::DragFloat("大小", &orthoSize, 0.1f, 0.0f, 0.0f))
                    {
                        orthoSize = std::max(orthoSize, 0.01f);
                        camera.SetOrthographicSize(orthoSize);
                        m_FrameModified = true;
                    }

                    float orthoNear = camera.GetOrthographicNearClip();
                    if (ImGui::DragFloat("近平面", &orthoNear, 0.1f))
                    {
                        camera.SetOrthographicNearClip(orthoNear);
                        m_FrameModified = true;
                    }

                    float orthoFar = camera.GetOrthographicFarClip();
                    if (ImGui::DragFloat("远平面", &orthoFar, 0.1f))
                    {
                        orthoFar = std::max(orthoFar, camera.GetOrthographicNearClip() + 0.1f);
                        camera.SetOrthographicFarClip(orthoFar);
                        m_FrameModified = true;
                    }

                    m_FrameModified |= ImGui::Checkbox("固定宽高比", &component.FixedAspectRatio);
                }
            });

        // Light — 通过反射自动绘制（见下方统一循环）
        // RigidBody — 通过反射自动绘制
        // BoxCollider — 通过反射自动绘制
        // SphereCollider — 通过反射自动绘制
        // CollisionParticleTrigger — 通过反射自动绘制

        // Mesh Renderer
        DrawComponent<MeshRendererComponent>(
            "\u7f51\u683c\u6e32\u67d3\u5668", entity, [this](auto& component)
            { m_FrameModified |= PropertiesPanelCustomDrawers::DrawMeshRendererInspector(component); });
        // RigidBody — 通过反射自动绘制（见下方统一循环）

        // BoxCollider — 通过反射自动绘制

        // SphereCollider — 通过反射自动绘制

        // Terrain
        DrawComponent<TerrainComponent>(
            "\u5730\u5f62", entity, [this](auto& component)
            { m_FrameModified |= PropertiesPanelCustomDrawers::DrawTerrainInspector(component); });
        // ParticleEmitter
        DrawComponent<ParticleEmitterComponent>(
            "粒子发射器", entity, [this](auto& component)
            { m_FrameModified |= PropertiesPanelCustomDrawers::DrawParticleEmitterInspector(component); });
        // CollisionParticleTrigger — 通过反射自动绘制

        // AudioSource
        DrawComponent<AudioSourceComponent>(
            "\u97f3\u9891\u6e90", entity,
            [this, entity](auto& component)
            {
                const AudioRuntimeState* audioState = nullptr;
                auto*                    scene      = entity.GetScene();
                if (scene && scene->GetSceneRenderer())
                    audioState = scene->GetSceneRenderer()->GetAudioSystem().GetStore().Get(
                        static_cast<uint32_t>((entt::entity)entity));
                m_FrameModified |= PropertiesPanelCustomDrawers::DrawAudioSourceInspector(component, audioState);
            });
        // AudioListener
        DrawComponent<AudioListenerComponent>(
            "\u97f3\u9891\u76d1\u542c\u5668", entity, [this](auto& component)
            { m_FrameModified |= PropertiesPanelCustomDrawers::DrawAudioListenerInspector(component); });
        // VideoPlayer
        DrawComponent<VideoPlayerComponent>(
            "\u89c6\u9891\u64ad\u653e\u5668", entity,
            [this, entity](auto& component)
            {
                const VideoRuntimeState* videoState = nullptr;
                auto*                    scene      = entity.GetScene();
                if (scene && scene->GetSceneRenderer())
                    videoState = scene->GetSceneRenderer()->GetVideoSystem().GetStore().Get(
                        static_cast<uint32_t>((entt::entity)entity));
                m_FrameModified |= PropertiesPanelCustomDrawers::DrawVideoPlayerInspector(component, videoState);
            });
        // ---- 反射组件统一绘制 ----
        {
            // DrawVec3Control 适配函数（包装成 AutoInspector 需要的签名）
            static PropertiesPanel* s_Panel = nullptr;
            s_Panel                         = this;
            auto drawVec3Wrapper            = [](const char* label, float* values, float resetValue)
            {
                auto state = s_Panel->DrawVec3Control(label, *reinterpret_cast<glm::vec3*>(values), resetValue);
                if (state.ValueChanged)
                    s_Panel->m_FrameModified = true;
            };

            auto*    scene    = entity.GetScene();
            uint32_t entityId = static_cast<uint32_t>(static_cast<entt::entity>(entity));

            for (auto& meta : ComponentRegistry::Instance().GetAll())
            {
                if (scene && meta.Has(*scene, entityId) && !(meta.Flags & ComponentMeta::CustomUI))
                {
                    DrawComponent_Auto(meta.DisplayName, entity, meta, drawVec3Wrapper);
                }
            }
        }

        // ---- NativeScript 组件 ----
        DrawComponent<NativeScriptComponent>(
            "\u811a\u672c", entity, [this](auto& component)
            { m_FrameModified |= PropertiesPanelCustomDrawers::DrawNativeScriptInspector(component); });
    }

} // namespace Engine
