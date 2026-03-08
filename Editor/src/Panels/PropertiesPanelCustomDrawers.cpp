#include "Panels/PropertiesPanelCustomDrawers.h"

#include "Asset/AssetManager.h"
#include "Asset/PathUtils.h"
#include "Core/FileDialogs.h"
#include "Core/Log.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Script/ScriptRegistry.h"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

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

        bool TryNormalizeProjectAssetPath(const std::string& candidatePath, const char* assetLabel, std::string& outPath,
                                          bool warnOnFailure = true)
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

            component.Type = MeshType::Model;
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
        void DrawMeshRendererInspector(MeshRendererComponent& component)
        {
            ImGui::ColorEdit4("颜色", glm::value_ptr(component.Color));

            const char* meshTypeLabels[] = {"Cube", "Plane", "Sphere", "Model"};
            int currentIdx = static_cast<int>(component.Type);
            if (currentIdx > 3)
                currentIdx = 0;

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

            auto acceptModelDrop = [&component]()
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MODEL"))
                {
                    ApplyModelMeshPath(component, static_cast<const char*>(payload->Data));
                    return true;
                }

                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
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
                    ApplyModelMeshPath(component, relStr);
            }

            const std::string& modelPath = AssetManager::GetPath<Mesh>(component.MeshAsset);
            char modelPathBuf[256];
            memset(modelPathBuf, 0, sizeof(modelPathBuf));
            std::strncpy(modelPathBuf, modelPath.c_str(), sizeof(modelPathBuf) - 1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("模型路径", modelPathBuf, sizeof(modelPathBuf), ImGuiInputTextFlags_EnterReturnsTrue))
                ApplyModelMeshPath(component, modelPathBuf);

            if (ImGui::BeginDragDropTarget())
            {
                acceptModelDrop();
                ImGui::EndDragDropTarget();
            }

            ImGui::Button("拖拽模型到此处", ImVec2(-FLT_MIN, 0.0f));
            if (ImGui::BeginDragDropTarget())
            {
                acceptModelDrop();
                ImGui::EndDragDropTarget();
            }

            ImGui::Separator();
            ImGui::Text("PBR 材质");
            ImGui::DragFloat("金属度", &component.Metallic, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::DragFloat("粗糙度", &component.Roughness, 0.01f, 0.0f, 1.0f, "%.2f");

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

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE"))
                    {
                        std::string droppedPath(static_cast<const char*>(payload->Data));
                        component.DiffuseTextureAsset = AssetManager::Load<Texture2D>(droppedPath);
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("浏览..."))
            {
                std::string relStr;
                if (TrySelectProjectAssetPath("*.png;*.jpg;*.jpeg;*.bmp;*.tga", "图片文件", "纹理", relStr))
                    component.DiffuseTextureAsset = AssetManager::Load<Texture2D>(relStr);
            }

            if (ImGui::Button("加载纹理"))
            {
                const std::string& texPath = AssetManager::GetPath<Texture2D>(component.DiffuseTextureAsset);
                if (!texPath.empty())
                    component.DiffuseTextureAsset = AssetManager::Load<Texture2D>(texPath);
            }
            ImGui::SameLine();
            if (ImGui::Button("清除纹理"))
                component.DiffuseTextureAsset = {};

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

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE"))
                    {
                        std::string droppedPath(static_cast<const char*>(payload->Data));
                        component.NormalMapAsset = AssetManager::Load<Texture2D>(droppedPath);
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("浏览##NormalMap"))
            {
                std::string relStr;
                if (TrySelectProjectAssetPath("*.png;*.jpg;*.jpeg;*.bmp;*.tga", "法线贴图", "法线贴图", relStr))
                    component.NormalMapAsset = AssetManager::Load<Texture2D>(relStr);
            }

            if (ImGui::Button("加载法线贴图"))
            {
                const std::string& normalPath = AssetManager::GetPath<Texture2D>(component.NormalMapAsset);
                if (!normalPath.empty())
                    component.NormalMapAsset = AssetManager::Load<Texture2D>(normalPath);
            }
            ImGui::SameLine();
            if (ImGui::Button("清除法线贴图"))
                component.NormalMapAsset = {};
        }

        void DrawTerrainInspector(TerrainComponent& component)
        {
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
                std::string relStr;
                if (TrySelectProjectAssetPath("*.png;*.jpg;*.bmp;*.tga", "高度图", "高度图", relStr))
                {
                    component.HeightmapPath = relStr;
                    component.MeshDirty = true;
                }
            }

            if (ImGui::Button("重新生成网格"))
                component.MeshDirty = true;

            if (ImGui::DragFloat("高度缩放", &component.HeightScale, 0.5f, 0.1f, 500.0f, "%.1f"))
                component.MeshDirty = true;
            if (ImGui::DragFloat("地形尺寸", &component.TerrainSize, 1.0f, 1.0f, 1000.0f, "%.1f"))
                component.MeshDirty = true;

            ImGui::Separator();

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
                std::string relStr;
                if (TrySelectProjectAssetPath("*.png;*.jpg;*.bmp;*.tga", "Splatmap", "Splatmap", relStr))
                    component.SplatmapPath = relStr;
            }

            ImGui::Separator();

            const char* layerNames[] = {"层0 (草地)", "层1 (泥土)", "层2 (岩石)", "层3 (雪地)"};
            for (int i = 0; i < 4; i++)
            {
                ImGui::PushID(i);
                if (ImGui::TreeNode(layerNames[i]))
                {
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
                        std::string relStr;
                        if (TrySelectProjectAssetPath("*.png;*.jpg;*.bmp;*.tga", "贴图", "贴图", relStr))
                            component.LayerTextures[i] = AssetManager::Load<Texture2D>(relStr);
                    }

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

            ImGui::Text("物理");
            ImGui::DragFloat("摩擦力", &component.Friction, 0.01f, 0.0f, 2.0f, "%.2f");
            ImGui::DragFloat("弹性", &component.Restitution, 0.01f, 0.0f, 1.0f, "%.2f");

            ImGui::Separator();

            ImGui::Text("细节层次 (LOD)");
            if (ImGui::SliderInt("LOD 层数", &component.LODLevels, 1, 3))
                component.MeshDirty = true;
            ImGui::DragFloat("LOD1 距离", &component.LODDistance1, 1.0f, 10.0f, 500.0f, "%.0f");
            ImGui::DragFloat("LOD2 距离", &component.LODDistance2, 1.0f, 20.0f, 1000.0f, "%.0f");

            ImGui::Separator();

            ImGui::Text("草地");
            ImGui::Checkbox("启用草地", &component.GrassEnabled);
            if (component.GrassEnabled)
            {
                ImGui::DragFloat("草密度", &component.GrassDensity, 0.5f, 0.1f, 50.0f, "%.1f 片/m2");
                ImGui::DragFloat("草高度", &component.GrassHeight, 0.01f, 0.01f, 2.0f, "%.2f");
                ImGui::DragFloat("草宽度", &component.GrassWidth, 0.01f, 0.01f, 1.0f, "%.2f");
                ImGui::DragFloat("风力", &component.GrassWindStrength, 0.01f, 0.0f, 2.0f, "%.2f");

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
        }

        void DrawAudioSourceInspector(AudioSourceComponent& component)
        {
            char pathBuf[256];
            memset(pathBuf, 0, sizeof(pathBuf));
            std::strncpy(pathBuf, component.AudioPath.c_str(), sizeof(pathBuf) - 1);
            if (ImGui::InputText("音频文件", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                std::string normalizedPath;
                if (TryNormalizeProjectAssetPath(pathBuf, "音频", normalizedPath))
                    component.AudioPath = normalizedPath;
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_AUDIO"))
                {
                    std::string normalizedPath;
                    if (TryNormalizeProjectAssetPath(static_cast<const char*>(payload->Data), "音频", normalizedPath, false))
                        component.AudioPath = normalizedPath;
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (ImGui::Button("浏览##Audio"))
            {
                std::string relStr;
                if (TrySelectProjectAssetPath("*.wav", "WAV 音频文件", "音频", relStr))
                    component.AudioPath = relStr;
            }

            ImGui::SliderFloat("音量", &component.Volume, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("音调", &component.Pitch, 0.1f, 3.0f, "%.2f");

            ImGui::Separator();
            ImGui::Text("3D 空间音效");
            ImGui::Checkbox("空间化", &component.Spatial);
            if (component.Spatial)
            {
                ImGui::DragFloat("最小距离", &component.MinDistance, 0.1f, 0.1f, 100.0f, "%.1f");
                ImGui::DragFloat("最大距离", &component.MaxDistance, 1.0f, 1.0f, 500.0f, "%.0f");
            }

            ImGui::Separator();
            ImGui::Checkbox("循环", &component.Loop);
            ImGui::Checkbox("启动时播放", &component.PlayOnStart);

            if (component.RuntimeSource != 0)
            {
                ImGui::Separator();
                ImGui::Text("状态: %s", component.IsPlaying ? "播放中" : "已停止");
            }
        }

        void DrawAudioListenerInspector(AudioListenerComponent& component)
        {
            ImGui::Checkbox("激活", &component.Active);
            ImGui::TextWrapped("场景中只有一个监听器应当激活。监听器位置跟随实体变换。");
        }

        void DrawVideoPlayerInspector(VideoPlayerComponent& component)
        {
            char urlBuf[512];
            memset(urlBuf, 0, sizeof(urlBuf));
            std::strncpy(urlBuf, component.StreamURL.c_str(), sizeof(urlBuf) - 1);
            if (ImGui::InputText("流地址", urlBuf, sizeof(urlBuf), ImGuiInputTextFlags_EnterReturnsTrue))
                component.StreamURL = std::string(urlBuf);

            ImGui::TextWrapped("支持 rtmp:// 或本地文件路径");
            ImGui::SliderFloat("音量##Video", &component.Volume, 0.0f, 2.0f, "%.2f");
            ImGui::Checkbox("启动时播放##Video", &component.PlayOnStart);
            ImGui::Checkbox("循环##Video", &component.Loop);

            if (component.RuntimeTexture)
            {
                ImGui::Separator();
                ImGui::Text("视频预览");
                float w = ImGui::GetContentRegionAvail().x;
                float aspect = (float)component.RuntimeTexture->GetWidth() / (float)component.RuntimeTexture->GetHeight();
                float h = w / aspect;
                ImGui::Image((ImTextureID)(uintptr_t)component.RuntimeTexture->GetRendererID(),
                             ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
            }

            if (component.RuntimeDecoder)
            {
                ImGui::Separator();
                ImGui::Text("状态: %s", component.IsPlaying ? "播放中" : "已停止");
            }
        }

        void DrawNativeScriptInspector(NativeScriptComponent& component)
        {
            auto& scripts = ScriptRegistry::Instance().GetAll();
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
                return;

            if (ImGui::Selectable("(无)", component.ScriptName.empty()))
            {
                component.ScriptName.clear();
                component.InstantiateScript = nullptr;
                component.DestroyScript = nullptr;
                if (component.Instance)
                    component.Instance.reset();
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
    } // namespace PropertiesPanelCustomDrawers
} // namespace Engine
