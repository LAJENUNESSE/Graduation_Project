#include "Panels/PropertiesPanelCustomDrawers.h"

#include "Asset/AssetManager.h"
#include "Asset/AssetType.h"
#include "Asset/PathUtils.h"
#include "Core/FileDialogs.h"
#include "Core/Log.h"
#include "EditorAssetDescriptor.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Scene/Runtime/AudioRuntimeStore.h"
#include "Scene/Runtime/VideoRuntimeStore.h"
#include "Script/ScriptRegistry.h"

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>

namespace Engine
{
    namespace
    {
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

        bool TryNormalizeProjectAssetPath(const std::string& candidatePath,
                                          const char*        assetLabel,
                                          std::string&       outPath,
                                          bool               warnOnFailure = true)
        {
            if (candidatePath.empty())
            {
                outPath.clear();
                return true;
            }

            if (PathUtils::IsSafeAssetPath(candidatePath))
            {
                outPath = PathUtils::NormalizeSeparators(candidatePath);
                return true;
            }

            if (PathUtils::TryToProjectRelative(candidatePath, outPath))
                return true;

            if (warnOnFailure)
                ENGINE_WARN("{}必须位于项目目录内: {}", assetLabel, candidatePath);
            return false;
        }

        void ApplyModelMeshAsset(MeshRendererComponent& component, AssetHandle meshHandle)
        {
            if (!meshHandle.IsValid())
                return;

            component.Type      = MeshType::Model;
            component.MeshAsset = meshHandle;

            Mesh* mesh = AssetManager::Get<Mesh>(meshHandle);
            if (!mesh)
                return;

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

        void ApplyModelMeshPath(MeshRendererComponent& component, const std::string& path)
        {
            std::string normalizedPath;
            if (!TryNormalizeProjectAssetPath(path, "模型", normalizedPath))
                return;

            ApplyModelMeshAsset(component, AssetManager::Load<Mesh>(normalizedPath));
        }
    } // namespace

    namespace PropertiesPanelCustomDrawers
    {
        namespace
        {
            bool DrawVec3Control(const std::string& label,
                                 glm::vec3&         values,
                                 float              resetValue  = 0.0f,
                                 float              columnWidth = 100.0f)
            {
                bool     modified = false;
                ImGuiIO& io       = ImGui::GetIO();
                ImFont*  boldFont = io.Fonts->Fonts[0];

                ImGui::PushID(label.c_str());
                ImGui::Columns(2);
                ImGui::SetColumnWidth(0, columnWidth);
                ImGui::Text("%s", label.c_str());
                ImGui::NextColumn();

                ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0f, 0.0f});

                const float  lineHeight = ImGui::GetFrameHeight();
                const ImVec2 buttonSize = {lineHeight + 3.0f, lineHeight};

                auto drawAxis = [&](const char* axisLabel, int axisIndex, const ImVec4& color, const ImVec4& hoverColor)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, color);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
                    ImGui::PushFont(boldFont);
                    if (ImGui::Button(axisLabel, buttonSize))
                    {
                        values[axisIndex] = resetValue;
                        modified          = true;
                    }
                    ImGui::PopFont();
                    ImGui::PopStyleColor(3);
                    ImGui::SameLine();
                    modified |= ImGui::DragFloat((std::string("##") + axisLabel).c_str(), &values[axisIndex], 0.1f,
                                                 0.0f, 0.0f, "%.2f");
                    ImGui::PopItemWidth();
                };

                drawAxis("X", 0, ImVec4{0.8f, 0.1f, 0.15f, 1.0f}, ImVec4{0.9f, 0.2f, 0.2f, 1.0f});
                ImGui::SameLine();
                drawAxis("Y", 1, ImVec4{0.2f, 0.7f, 0.2f, 1.0f}, ImVec4{0.3f, 0.8f, 0.3f, 1.0f});
                ImGui::SameLine();
                drawAxis("Z", 2, ImVec4{0.1f, 0.25f, 0.8f, 1.0f}, ImVec4{0.2f, 0.35f, 0.9f, 1.0f});

                ImGui::PopStyleVar();
                ImGui::Columns(1);
                ImGui::PopID();

                return modified;
            }
        } // namespace

        bool DrawMeshRendererInspector(MeshRendererComponent& component)
        {
            bool modified = false;
            modified |= ImGui::ColorEdit4("颜色", glm::value_ptr(component.Color));

            const char* meshTypeLabels[] = {"Cube", "Plane", "Sphere", "Model"};
            int         currentIdx       = static_cast<int>(component.Type);
            if (currentIdx > 3)
                currentIdx = 0;

            if (ImGui::BeginCombo("网格", meshTypeLabels[currentIdx]))
            {
                if (ImGui::Selectable("Cube", component.Type == MeshType::Cube))
                {
                    component.Type      = MeshType::Cube;
                    component.MeshAsset = AssetManager::Load<Mesh>("builtin:Cube");
                    modified            = true;
                }
                if (ImGui::Selectable("Plane", component.Type == MeshType::Plane))
                {
                    component.Type      = MeshType::Plane;
                    component.MeshAsset = AssetManager::Load<Mesh>("builtin:Plane");
                    modified            = true;
                }
                if (ImGui::Selectable("Sphere", component.Type == MeshType::Sphere))
                {
                    component.Type      = MeshType::Sphere;
                    component.MeshAsset = AssetManager::Load<Mesh>("builtin:Sphere");
                    modified            = true;
                }
                ImGui::EndCombo();
            }

            auto acceptModelDrop = [&component]()
            {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload(GetEditorAssetDescriptor(AssetType::Mesh).PayloadType))
                {
                    ApplyModelMeshPath(component, static_cast<const char*>(payload->Data));
                    return true;
                }

                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload(GetEditorAssetDescriptor(AssetType::None).PayloadType))
                {
                    ApplyModelMeshPath(component, static_cast<const char*>(payload->Data));
                    return true;
                }

                return false;
            };

            if (ImGui::Button("导入模型..."))
            {
                std::string relStr;
                if (TrySelectProjectAssetPath("*.obj;*.fbx;*.gltf;*.glb", "3D模型文件", "模型", relStr))
                {
                    ApplyModelMeshPath(component, relStr);
                    modified = true;
                }
            }

            const std::string& modelPath = AssetManager::GetPath<Mesh>(component.MeshAsset);
            char               modelPathBuf[256];
            memset(modelPathBuf, 0, sizeof(modelPathBuf));
            std::strncpy(modelPathBuf, modelPath.c_str(), sizeof(modelPathBuf) - 1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("模型路径", modelPathBuf, sizeof(modelPathBuf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                ApplyModelMeshPath(component, modelPathBuf);
                modified = true;
            }

            if (ImGui::BeginDragDropTarget())
            {
                modified |= acceptModelDrop();
                ImGui::EndDragDropTarget();
            }

            ImGui::Button("拖拽模型到此处", ImVec2(-FLT_MIN, 0.0f));
            if (ImGui::BeginDragDropTarget())
            {
                modified |= acceptModelDrop();
                ImGui::EndDragDropTarget();
            }

            ImGui::Separator();
            ImGui::Text("PBR 材质");
            modified |= ImGui::DragFloat("金属度", &component.Metallic, 0.01f, 0.0f, 1.0f, "%.2f");
            modified |= ImGui::DragFloat("粗糙度", &component.Roughness, 0.01f, 0.0f, 1.0f, "%.2f");

            {
                const std::string& texPath = AssetManager::GetPath<Texture2D>(component.DiffuseTextureAsset);
                char               texPathBuf[256];
                memset(texPathBuf, 0, sizeof(texPathBuf));
                std::strncpy(texPathBuf, texPath.c_str(), sizeof(texPathBuf) - 1);
                if (ImGui::InputText("纹理路径", texPathBuf, sizeof(texPathBuf), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    std::string newPath(texPathBuf);
                    if (!newPath.empty())
                        component.DiffuseTextureAsset = AssetManager::Load<Texture2D>(newPath);
                    else
                        component.DiffuseTextureAsset = {};
                    modified = true;
                }

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload(GetEditorAssetDescriptor(AssetType::Texture2D).PayloadType))
                    {
                        std::string droppedPath(static_cast<const char*>(payload->Data));
                        component.DiffuseTextureAsset = AssetManager::Load<Texture2D>(droppedPath);
                        modified                      = true;
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("浏览..."))
            {
                std::string relStr;
                if (TrySelectProjectAssetPath("*.png;*.jpg;*.jpeg;*.bmp;*.tga", "图片文件", "纹理", relStr))
                {
                    component.DiffuseTextureAsset = AssetManager::Load<Texture2D>(relStr);
                    modified                      = true;
                }
            }

            if (ImGui::Button("加载纹理"))
            {
                const std::string& texPath = AssetManager::GetPath<Texture2D>(component.DiffuseTextureAsset);
                if (!texPath.empty())
                {
                    component.DiffuseTextureAsset = AssetManager::Load<Texture2D>(texPath);
                    modified                      = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("清除纹理"))
            {
                component.DiffuseTextureAsset = {};
                modified                      = true;
            }

            modified |= ImGui::DragFloat("平铺 X", &component.Tiling.x, 0.1f, 0.01f, 100.0f, "%.2f");
            modified |= ImGui::DragFloat("平铺 Y", &component.Tiling.y, 0.1f, 0.01f, 100.0f, "%.2f");

            ImGui::Separator();
            ImGui::Text("法线贴图");
            {
                const std::string& normalPath = AssetManager::GetPath<Texture2D>(component.NormalMapAsset);
                char               normalPathBuf[256];
                memset(normalPathBuf, 0, sizeof(normalPathBuf));
                std::strncpy(normalPathBuf, normalPath.c_str(), sizeof(normalPathBuf) - 1);
                if (ImGui::InputText("法线贴图路径", normalPathBuf, sizeof(normalPathBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    std::string newPath(normalPathBuf);
                    if (!newPath.empty())
                        component.NormalMapAsset = AssetManager::Load<Texture2D>(newPath);
                    else
                        component.NormalMapAsset = {};
                    modified = true;
                }

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload(GetEditorAssetDescriptor(AssetType::Texture2D).PayloadType))
                    {
                        std::string droppedPath(static_cast<const char*>(payload->Data));
                        component.NormalMapAsset = AssetManager::Load<Texture2D>(droppedPath);
                        modified                 = true;
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("浏览##NormalMap"))
            {
                std::string relStr;
                if (TrySelectProjectAssetPath("*.png;*.jpg;*.jpeg;*.bmp;*.tga", "法线贴图", "法线贴图", relStr))
                {
                    component.NormalMapAsset = AssetManager::Load<Texture2D>(relStr);
                    modified                 = true;
                }
            }

            if (ImGui::Button("加载法线贴图"))
            {
                const std::string& normalPath = AssetManager::GetPath<Texture2D>(component.NormalMapAsset);
                if (!normalPath.empty())
                {
                    component.NormalMapAsset = AssetManager::Load<Texture2D>(normalPath);
                    modified                 = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("清除法线贴图"))
            {
                component.NormalMapAsset = {};
                modified                 = true;
            }

            return modified;
        }

        bool DrawTerrainInspector(TerrainComponent& component)
        {
            bool modified = false;
            ImGui::Text("高度图");
            {
                char buf[256];
                memset(buf, 0, sizeof(buf));
                std::strncpy(buf, component.HeightmapPath.c_str(), sizeof(buf) - 1);
                if (ImGui::InputText("高度图路径", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    component.HeightmapPath = std::string(buf);
                    component.MeshDirty     = true;
                    modified                = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("浏览##Heightmap"))
            {
                std::string relStr;
                if (TrySelectProjectAssetPath("*.png;*.jpg;*.bmp;*.tga", "高度图", "高度图", relStr))
                {
                    component.HeightmapPath = relStr;
                    component.MeshDirty     = true;
                    modified                = true;
                }
            }

            if (ImGui::Button("重新生成网格"))
            {
                component.MeshDirty = true;
                modified            = true;
            }

            if (ImGui::DragFloat("高度缩放", &component.HeightScale, 0.5f, 0.1f, 500.0f, "%.1f"))
            {
                component.MeshDirty = true;
                modified            = true;
            }
            if (ImGui::DragFloat("地形尺寸", &component.TerrainSize, 1.0f, 1.0f, 1000.0f, "%.1f"))
            {
                component.MeshDirty = true;
                modified            = true;
            }

            ImGui::Separator();

            ImGui::Text("Splat Map");
            {
                char buf[256];
                memset(buf, 0, sizeof(buf));
                std::strncpy(buf, component.SplatmapPath.c_str(), sizeof(buf) - 1);
                if (ImGui::InputText("Splatmap 路径", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    component.SplatmapPath = std::string(buf);
                    modified               = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("浏览##Splatmap"))
            {
                std::string relStr;
                if (TrySelectProjectAssetPath("*.png;*.jpg;*.bmp;*.tga", "Splatmap", "Splatmap", relStr))
                {
                    component.SplatmapPath = relStr;
                    modified               = true;
                }
            }

            ImGui::Separator();

            const char* layerNames[] = {"层0 (草地)", "层1 (泥土)", "层2 (岩石)", "层3 (雪地)"};
            for (int i = 0; i < 4; i++)
            {
                ImGui::PushID(i);
                if (ImGui::TreeNode(layerNames[i]))
                {
                    const std::string& texPath = AssetManager::GetPath<Texture2D>(component.LayerTextures[i]);
                    char               texBuf[256];
                    memset(texBuf, 0, sizeof(texBuf));
                    std::strncpy(texBuf, texPath.c_str(), sizeof(texBuf) - 1);
                    if (ImGui::InputText("反照率", texBuf, sizeof(texBuf), ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        std::string p(texBuf);
                        component.LayerTextures[i] = p.empty() ? AssetHandle{} : AssetManager::Load<Texture2D>(p);
                        modified                   = true;
                    }
                    ImGui::SameLine();
                    std::string browseId = "浏览##LayerTex" + std::to_string(i);
                    if (ImGui::Button(browseId.c_str()))
                    {
                        std::string relStr;
                        if (TrySelectProjectAssetPath("*.png;*.jpg;*.bmp;*.tga", "贴图", "贴图", relStr))
                        {
                            component.LayerTextures[i] = AssetManager::Load<Texture2D>(relStr);
                            modified                   = true;
                        }
                    }

                    const std::string& normPath = AssetManager::GetPath<Texture2D>(component.LayerNormalMaps[i]);
                    char               normBuf[256];
                    memset(normBuf, 0, sizeof(normBuf));
                    std::strncpy(normBuf, normPath.c_str(), sizeof(normBuf) - 1);
                    if (ImGui::InputText("法线", normBuf, sizeof(normBuf), ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        std::string p(normBuf);
                        component.LayerNormalMaps[i] = p.empty() ? AssetHandle{} : AssetManager::Load<Texture2D>(p);
                        modified                     = true;
                    }

                    modified |= ImGui::DragFloat("平铺", &component.LayerTiling[i], 0.5f, 0.1f, 100.0f, "%.1f");
                    modified |= ImGui::DragFloat("金属度", &component.LayerMetallic[i], 0.01f, 0.0f, 1.0f, "%.2f");
                    modified |= ImGui::DragFloat("粗糙度", &component.LayerRoughness[i], 0.01f, 0.0f, 1.0f, "%.2f");

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            ImGui::Separator();

            ImGui::Text("物理");
            modified |= ImGui::DragFloat("摩擦力", &component.Friction, 0.01f, 0.0f, 2.0f, "%.2f");
            modified |= ImGui::DragFloat("弹性", &component.Restitution, 0.01f, 0.0f, 1.0f, "%.2f");

            ImGui::Separator();

            ImGui::Text("细节层次 (LOD)");
            if (ImGui::SliderInt("LOD 层数", &component.LODLevels, 1, 3))
            {
                component.MeshDirty = true;
                modified            = true;
            }
            modified |= ImGui::DragFloat("LOD1 距离", &component.LODDistance1, 1.0f, 10.0f, 500.0f, "%.0f");
            modified |= ImGui::DragFloat("LOD2 距离", &component.LODDistance2, 1.0f, 20.0f, 1000.0f, "%.0f");

            ImGui::Separator();

            ImGui::Text("草地");
            modified |= ImGui::Checkbox("启用草地", &component.GrassEnabled);
            if (component.GrassEnabled)
            {
                modified |= ImGui::DragFloat("草密度", &component.GrassDensity, 0.5f, 0.1f, 50.0f, "%.1f 片/m2");
                modified |= ImGui::DragFloat("草高度", &component.GrassHeight, 0.01f, 0.01f, 2.0f, "%.2f");
                modified |= ImGui::DragFloat("草宽度", &component.GrassWidth, 0.01f, 0.01f, 1.0f, "%.2f");
                modified |= ImGui::DragFloat("风力", &component.GrassWindStrength, 0.01f, 0.0f, 2.0f, "%.2f");

                const std::string& grassPath = AssetManager::GetPath<Texture2D>(component.GrassTexture);
                char               grassBuf[256];
                memset(grassBuf, 0, sizeof(grassBuf));
                std::strncpy(grassBuf, grassPath.c_str(), sizeof(grassBuf) - 1);
                if (ImGui::InputText("草贴图", grassBuf, sizeof(grassBuf), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    std::string p(grassBuf);
                    component.GrassTexture = p.empty() ? AssetHandle{} : AssetManager::Load<Texture2D>(p);
                    modified               = true;
                }
            }

            return modified;
        }

        bool DrawParticleEmitterInspector(ParticleEmitterComponent& component)
        {
            bool        changed       = false;
            const char* presetNames[] = {"自定义", "火焰", "烟雾", "爆炸", "火花"};
            int         presetIdx     = static_cast<int>(component.CurrentPreset);
            if (ImGui::Combo("预设", &presetIdx, presetNames, 5))
            {
                auto preset = static_cast<ParticleEmitterComponent::Preset>(presetIdx);
                if (preset != ParticleEmitterComponent::Preset::Custom)
                    ParticleEmitterComponent::ApplyPreset(component, preset);
                else
                    component.CurrentPreset = ParticleEmitterComponent::Preset::Custom;
                changed = true;
            }

            ImGui::Separator();
            ImGui::Text("发射");
            changed |= ImGui::DragFloat("发射速率", &component.EmitRate, 1.0f, 0.0f, 100000.0f, "%.0f");
            ImGui::SameLine();
            ImGui::TextDisabled("粒子/秒");
            changed |= ImGui::DragInt("爆发数量", &component.BurstCount, 1, 0, 10000);
            ImGui::SameLine();
            if (ImGui::Button("触发爆发"))
            {
                component.PendingBurst = component.BurstCount;
                changed                = true;
            }

            int maxParticles = static_cast<int>(component.MaxParticles);
            if (ImGui::DragInt("最大粒子数", &maxParticles, 100, 100, 1000000))
            {
                component.MaxParticles = static_cast<uint32_t>(std::max(maxParticles, 100));
                changed                = true;
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
            changed |= DrawVec3Control("发射方向", component.EmitDirection);
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
            changed |= DrawVec3Control("重力", component.Gravity);
            changed |= ImGui::DragFloat("阻尼", &component.Damping, 0.01f, 0.0f, 1.0f, "%.2f");

            ImGui::Separator();
            const char* blendModes[] = {"加法混合", "Alpha混合"};
            int         blendIdx     = static_cast<int>(component.Blend);
            if (ImGui::Combo("混合模式", &blendIdx, blendModes, 2))
            {
                component.Blend = static_cast<ParticleEmitterComponent::BlendMode>(blendIdx);
                changed         = true;
            }

            if (changed && component.CurrentPreset != ParticleEmitterComponent::Preset::Custom)
                component.CurrentPreset = ParticleEmitterComponent::Preset::Custom;

            ImGui::Separator();
            ImGui::Text("SPH 流体");
            changed |= ImGui::Checkbox("启用 SPH", &component.SPH.Enabled);
            if (!component.SPH.Enabled)
                return changed;

            changed |= ImGui::DragFloat("静止密度", &component.SPH.RestDensity, 10.0f, 100.0f, 10000.0f, "%.0f");
            changed |= ImGui::DragFloat("气体常数", &component.SPH.GasConstant, 10.0f, 100.0f, 50000.0f, "%.0f");
            changed |= ImGui::DragFloat("粘性系数", &component.SPH.Viscosity, 0.001f, 0.0f, 1.0f, "%.4f");
            changed |= ImGui::DragFloat("光滑半径", &component.SPH.SmoothingRadius, 0.01f, 0.01f, 2.0f, "%.3f");
            changed |= ImGui::DragFloat("粒子质量", &component.SPH.ParticleMass, 0.001f, 0.001f, 1.0f, "%.4f");

            ImGui::Separator();
            ImGui::Text("PCISPH");
            changed |= ImGui::Checkbox("启用 PCISPH", &component.SPH.PCISPHEnabled);
            if (component.SPH.PCISPHEnabled)
            {
                changed |= ImGui::SliderInt("PCISPH 迭代次数", &component.SPH.PCISPHIterations, 1, 8);
                changed |= ImGui::DragFloat("PCISPH 校正系数", &component.SPH.PCISPHDelta, 0.01f, 0.01f, 1.0f, "%.3f");
            }

            ImGui::Separator();
            ImGui::Text("表面张力");
            changed |= ImGui::DragFloat("表面张力系数", &component.SPH.SurfaceTension, 0.1f, 0.0f, 20.0f, "%.2f");

            ImGui::Separator();
            ImGui::Text("刚体耦合");
            changed |= ImGui::Checkbox("启用刚体耦合", &component.SPH.RigidBodyCoupling);
            if (component.SPH.RigidBodyCoupling)
            {
                changed |=
                    ImGui::DragFloat("边界刚度", &component.SPH.BoundaryStiffness, 100.0f, 100.0f, 50000.0f, "%.0f");
                changed |= ImGui::DragFloat("边界阻尼", &component.SPH.BoundaryDamping, 0.01f, 0.0f, 1.0f, "%.2f");
            }

            return changed;
        }

        bool DrawFluidEmitterInspector(FluidEmitterComponent& component)
        {
            bool        changed       = false;
            const char* presetNames[] = {"自定义", "水龙头水流", "泥浆流", "山体喷发"};
            int         presetIdx     = static_cast<int>(component.CurrentPreset);
            if (ImGui::Combo("预设", &presetIdx, presetNames, 4))
            {
                auto preset = static_cast<FluidEmitterComponent::Preset>(presetIdx);
                if (preset != FluidEmitterComponent::Preset::Custom)
                    FluidEmitterComponent::ApplyPreset(component, preset);
                else
                    component.CurrentPreset = FluidEmitterComponent::Preset::Custom;
                changed = true;
            }

            ImGui::Separator();
            ImGui::Text("发射参数");
            int particleCount = static_cast<int>(component.ParticleCount);
            if (ImGui::DragInt("粒子数量", &particleCount, 100.0f, 100, 300000))
            {
                component.ParticleCount = static_cast<uint32_t>(std::max(particleCount, 100));
                changed                 = true;
            }
            changed |= ImGui::DragFloat("粒子半径", &component.ParticleRadius, 0.001f, 0.001f, 0.2f, "%.3f");
            changed |= DrawVec3Control("发射半尺寸", component.EmitExtents);
            changed |= DrawVec3Control("初始速度", component.InitialVelocity);

            ImGui::Separator();
            ImGui::Text("动力学");
            changed |= ImGui::DragFloat("静止密度", &component.RestDensity, 5.0f, 100.0f, 5000.0f, "%.1f");
            changed |= ImGui::DragFloat("气体常数", &component.GasConstant, 1.0f, 1.0f, 500.0f, "%.1f");
            changed |= ImGui::DragFloat("粘度", &component.Viscosity, 0.1f, 0.0f, 100.0f, "%.2f");
            changed |= ImGui::DragFloat("核半径", &component.SmoothingRadius, 0.001f, 0.01f, 0.5f, "%.3f");
            changed |= ImGui::DragFloat("粒子质量", &component.ParticleMass, 0.001f, 0.001f, 1.0f, "%.3f");
            changed |= DrawVec3Control("重力", component.Gravity);
            changed |= ImGui::DragFloat("阻尼", &component.Damping, 0.001f, 0.9f, 1.0f, "%.3f");

            ImGui::Separator();
            ImGui::Text("Mesh SDF 碰撞");
            changed |= ImGui::Checkbox("刚体耦合", &component.RigidBodyCoupling);
            changed |= ImGui::Checkbox("Mesh SDF 耦合", &component.MeshSDFCoupling);
            changed |= ImGui::DragInt("SDF 分辨率", &component.MeshSDFResolution, 1.0f, 8, 64);
            changed |= ImGui::DragFloat("SDF 带宽", &component.MeshSDFBand, 0.001f, 0.0f, 0.5f, "%.3f");
            changed |= ImGui::DragFloat("边界刚度", &component.BoundaryStiffness, 100.0f, 0.0f, 50000.0f, "%.0f");
            changed |= ImGui::DragFloat("边界阻尼", &component.BoundaryDamping, 0.01f, 0.0f, 1.0f, "%.2f");

            if (changed && component.CurrentPreset != FluidEmitterComponent::Preset::Custom)
                component.CurrentPreset = FluidEmitterComponent::Preset::Custom;

            return changed;
        }

        bool DrawAudioSourceInspector(AudioSourceComponent& component, const AudioRuntimeState* runtimeState)
        {
            bool modified = false;
            char pathBuf[256];
            memset(pathBuf, 0, sizeof(pathBuf));
            std::strncpy(pathBuf, component.AudioPath.c_str(), sizeof(pathBuf) - 1);
            if (ImGui::InputText("音频文件", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                std::string normalizedPath;
                if (TryNormalizeProjectAssetPath(pathBuf, "音频", normalizedPath))
                {
                    component.AudioPath = normalizedPath;
                    modified            = true;
                }
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload(GetEditorAssetDescriptor(AssetType::Audio).PayloadType))
                {
                    std::string normalizedPath;
                    if (TryNormalizeProjectAssetPath(static_cast<const char*>(payload->Data), "音频", normalizedPath,
                                                     false))
                    {
                        component.AudioPath = normalizedPath;
                        modified            = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (ImGui::Button("浏览##Audio"))
            {
                std::string relStr;
                if (TrySelectProjectAssetPath("*.wav", "WAV 音频文件", "音频", relStr))
                {
                    component.AudioPath = relStr;
                    modified            = true;
                }
            }

            modified |= ImGui::SliderFloat("音量", &component.Volume, 0.0f, 2.0f, "%.2f");
            modified |= ImGui::SliderFloat("音调", &component.Pitch, 0.1f, 3.0f, "%.2f");

            ImGui::Separator();
            ImGui::Text("3D 空间音效");
            modified |= ImGui::Checkbox("空间化", &component.Spatial);
            if (component.Spatial)
            {
                modified |= ImGui::DragFloat("最小距离", &component.MinDistance, 0.1f, 0.1f, 100.0f, "%.1f");
                modified |= ImGui::DragFloat("最大距离", &component.MaxDistance, 1.0f, 1.0f, 500.0f, "%.0f");
            }

            ImGui::Separator();
            modified |= ImGui::Checkbox("循环", &component.Loop);
            modified |= ImGui::Checkbox("启动时播放", &component.PlayOnStart);

            if (runtimeState && runtimeState->Source != 0)
            {
                ImGui::Separator();
                ImGui::Text("状态: %s", runtimeState->IsPlaying ? "播放中" : "已停止");
            }

            return modified;
        }

        bool DrawAudioListenerInspector(AudioListenerComponent& component)
        {
            bool modified = false;
            modified |= ImGui::Checkbox("激活", &component.Active);
            ImGui::TextWrapped("场景中只有一个监听器应当激活。监听器位置跟随实体变换。");

            return modified;
        }

        bool DrawVideoPlayerInspector(VideoPlayerComponent& component, const VideoRuntimeState* runtimeState)
        {
            bool modified = false;
            char urlBuf[512];
            memset(urlBuf, 0, sizeof(urlBuf));
            std::strncpy(urlBuf, component.StreamURL.c_str(), sizeof(urlBuf) - 1);
            if (ImGui::InputText("流地址", urlBuf, sizeof(urlBuf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                component.StreamURL = std::string(urlBuf);
                modified            = true;
            }

            ImGui::TextWrapped("支持 rtmp:// 或本地文件路径");
            modified |= ImGui::SliderFloat("音量##Video", &component.Volume, 0.0f, 2.0f, "%.2f");
            modified |= ImGui::Checkbox("启动时播放##Video", &component.PlayOnStart);
            modified |= ImGui::Checkbox("循环##Video", &component.Loop);

            if (runtimeState && runtimeState->Texture)
            {
                ImGui::Separator();
                ImGui::Text("视频预览");
                float w      = ImGui::GetContentRegionAvail().x;
                float aspect = (float)runtimeState->Texture->GetWidth() / (float)runtimeState->Texture->GetHeight();
                float h      = w / aspect;
                ImGui::Image((ImTextureID)(uintptr_t)runtimeState->Texture->GetRendererID(), ImVec2(w, h), ImVec2(0, 1),
                             ImVec2(1, 0));
            }

            if (runtimeState && runtimeState->Decoder)
            {
                ImGui::Separator();
                ImGui::Text("状态: %s", runtimeState->IsPlaying ? "播放中" : "已停止");
            }

            return modified;
        }

        bool DrawNativeScriptInspector(NativeScriptComponent& component)
        {
            bool        modified    = false;
            auto&       scripts     = ScriptRegistry::Instance().GetAll();
            const char* currentName = component.ScriptName.empty() ? "(无)" : component.ScriptName.c_str();

            for (auto& [name, entry] : scripts)
            {
                if (name == component.ScriptName)
                {
                    currentName = entry.DisplayName;
                    break;
                }
            }

            if (!ImGui::BeginCombo("脚本类", currentName))
                return false;

            if (ImGui::Selectable("(无)", component.ScriptName.empty()))
            {
                component.ScriptName.clear();
                component.InstantiateScript = nullptr;
                component.DestroyScript     = nullptr;
                if (component.Instance)
                    component.Instance.reset();
                modified = true;
            }

            for (auto& [name, entry] : scripts)
            {
                bool selected = (name == component.ScriptName);
                if (ImGui::Selectable(entry.DisplayName, selected))
                {
                    component.Instance.reset();
                    ScriptRegistry::Instance().Bind(component, name);
                    modified = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
            return modified;
        }
    } // namespace PropertiesPanelCustomDrawers
} // namespace Engine
