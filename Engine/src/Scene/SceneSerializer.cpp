#include "engpch.h"
#include "Scene/SceneSerializer.h"

#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Core/Log.h"

#include <yaml-cpp/yaml.h>

#include <glm/glm.hpp>
#include <filesystem>

namespace YAML
{

    template <>
    struct convert<glm::vec3>
    {
        static Node encode(const glm::vec3& rhs)
        {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.push_back(rhs.z);
            node.SetStyle(YAML::EmitterStyle::Flow);
            return node;
        }

        static bool decode(const Node& node, glm::vec3& rhs)
        {
            if (!node.IsSequence() || node.size() != 3)
                return false;

            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            rhs.z = node[2].as<float>();
            return true;
        }
    };

    template <>
    struct convert<glm::vec4>
    {
        static Node encode(const glm::vec4& rhs)
        {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.push_back(rhs.z);
            node.push_back(rhs.w);
            node.SetStyle(YAML::EmitterStyle::Flow);
            return node;
        }

        static bool decode(const Node& node, glm::vec4& rhs)
        {
            if (!node.IsSequence() || node.size() != 4)
                return false;

            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            rhs.z = node[2].as<float>();
            rhs.w = node[3].as<float>();
            return true;
        }
    };

    template <>
    struct convert<glm::vec2>
    {
        static Node encode(const glm::vec2& rhs)
        {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.SetStyle(YAML::EmitterStyle::Flow);
            return node;
        }

        static bool decode(const Node& node, glm::vec2& rhs)
        {
            if (!node.IsSequence() || node.size() != 2)
                return false;

            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            return true;
        }
    };

} // namespace YAML

namespace Engine
{

    // 统一路径安全校验：拒绝绝对路径（POSIX/Windows/UNC）与目录穿越
    static bool IsSafeAssetPath(const std::string& path)
    {
        if (path.empty())
            return false;

        // 使用 std::filesystem 判断绝对路径（跨平台：覆盖 /xxx, C:\xxx, \\server\xxx）
        std::filesystem::path p(path);
        if (p.is_absolute())
            return false;

        // 拒绝目录穿越（.. 和反斜杠变体）
        if (path.find("..") != std::string::npos)
            return false;

        // 拒绝含反斜杠的路径（防止 Windows 风格穿越如 ..\secret）
        if (path.find('\\') != std::string::npos)
            return false;

        return true;
    }

    static YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& v)
    {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
        return out;
    }

    static YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec4& v)
    {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << v.z << v.w << YAML::EndSeq;
        return out;
    }

    static YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec2& v)
    {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
        return out;
    }

    SceneSerializer::SceneSerializer(const Ref<Scene>& scene)
        : m_Scene(scene)
    {
    }

    static void SerializeEntity(YAML::Emitter& out, Entity entity)
    {
        out << YAML::BeginMap;

        // Entity ID
        if (entity.HasComponent<IDComponent>())
        {
            out << YAML::Key << "Entity" << YAML::Value << static_cast<uint64_t>(entity.GetComponent<IDComponent>().ID);
        }

        // TagComponent
        if (entity.HasComponent<TagComponent>())
        {
            out << YAML::Key << "TagComponent";
            out << YAML::BeginMap;
            auto& tag = entity.GetComponent<TagComponent>().Tag;
            out << YAML::Key << "Tag" << YAML::Value << tag;
            out << YAML::EndMap;
        }

        // TransformComponent
        if (entity.HasComponent<TransformComponent>())
        {
            out << YAML::Key << "TransformComponent";
            out << YAML::BeginMap;
            auto& tc = entity.GetComponent<TransformComponent>();
            out << YAML::Key << "Translation" << YAML::Value << tc.Translation;
            out << YAML::Key << "Rotation" << YAML::Value << tc.Rotation;
            out << YAML::Key << "Scale" << YAML::Value << tc.Scale;
            out << YAML::EndMap;
        }

        // MeshRendererComponent
        if (entity.HasComponent<MeshRendererComponent>())
        {
            out << YAML::Key << "MeshRendererComponent";
            out << YAML::BeginMap;
            auto& mrc = entity.GetComponent<MeshRendererComponent>();
            std::string meshType = "Cube";
            if (mrc.MeshData)
                meshType = mrc.MeshData->GetMeshType();
            out << YAML::Key << "MeshType" << YAML::Value << meshType;
            if (meshType == "Model" && mrc.MeshData)
                out << YAML::Key << "ModelPath" << YAML::Value << mrc.MeshData->GetModelPath();
            out << YAML::Key << "Color" << YAML::Value << mrc.Color;
            out << YAML::Key << "TexturePath" << YAML::Value << mrc.TexturePath;
            out << YAML::Key << "Tiling" << YAML::Value << mrc.Tiling;
            out << YAML::Key << "Shininess" << YAML::Value << mrc.Shininess;
            out << YAML::Key << "NormalMapPath" << YAML::Value << mrc.NormalMapPath;
            out << YAML::Key << "Metallic" << YAML::Value << mrc.Metallic;
            out << YAML::Key << "Roughness" << YAML::Value << mrc.Roughness;
            out << YAML::Key << "MetallicTexturePath" << YAML::Value << mrc.MetallicTexturePath;
            out << YAML::Key << "RoughnessTexturePath" << YAML::Value << mrc.RoughnessTexturePath;
            out << YAML::Key << "AOTexturePath" << YAML::Value << mrc.AOTexturePath;
            out << YAML::EndMap;
        }

        // LightComponent
        if (entity.HasComponent<LightComponent>())
        {
            out << YAML::Key << "LightComponent";
            out << YAML::BeginMap;
            auto& lc = entity.GetComponent<LightComponent>();
            out << YAML::Key << "Type" << YAML::Value << static_cast<int>(lc.Type);
            out << YAML::Key << "Color" << YAML::Value << lc.Color;
            out << YAML::Key << "Intensity" << YAML::Value << lc.Intensity;
            out << YAML::Key << "Constant" << YAML::Value << lc.Constant;
            out << YAML::Key << "Linear" << YAML::Value << lc.Linear;
            out << YAML::Key << "Quadratic" << YAML::Value << lc.Quadratic;
            out << YAML::Key << "InnerCutoff" << YAML::Value << lc.InnerCutoff;
            out << YAML::Key << "OuterCutoff" << YAML::Value << lc.OuterCutoff;
            out << YAML::Key << "CastShadows" << YAML::Value << lc.CastShadows;
            out << YAML::EndMap;
        }

        // CameraComponent
        if (entity.HasComponent<CameraComponent>())
        {
            out << YAML::Key << "CameraComponent";
            out << YAML::BeginMap;
            auto& cc = entity.GetComponent<CameraComponent>();
            auto& camera = cc.Camera;

            out << YAML::Key << "ProjectionType"
                << YAML::Value << static_cast<int>(camera.GetProjectionType());
            out << YAML::Key << "PerspectiveFOV" << YAML::Value << camera.GetPerspectiveVerticalFOV();
            out << YAML::Key << "PerspectiveNear" << YAML::Value << camera.GetPerspectiveNearClip();
            out << YAML::Key << "PerspectiveFar" << YAML::Value << camera.GetPerspectiveFarClip();
            out << YAML::Key << "OrthographicSize" << YAML::Value << camera.GetOrthographicSize();
            out << YAML::Key << "OrthographicNear" << YAML::Value << camera.GetOrthographicNearClip();
            out << YAML::Key << "OrthographicFar" << YAML::Value << camera.GetOrthographicFarClip();
            out << YAML::Key << "Primary" << YAML::Value << cc.Primary;
            out << YAML::Key << "FixedAspectRatio" << YAML::Value << cc.FixedAspectRatio;
            out << YAML::EndMap;
        }

        // RigidBodyComponent
        if (entity.HasComponent<RigidBodyComponent>())
        {
            out << YAML::Key << "RigidBodyComponent";
            out << YAML::BeginMap;
            auto& rb = entity.GetComponent<RigidBodyComponent>();
            out << YAML::Key << "Type" << YAML::Value << static_cast<int>(rb.Type);
            out << YAML::Key << "Mass" << YAML::Value << rb.Mass;
            out << YAML::Key << "Restitution" << YAML::Value << rb.Restitution;
            out << YAML::Key << "Friction" << YAML::Value << rb.Friction;
            out << YAML::Key << "GravityScale" << YAML::Value << rb.GravityScale;
            out << YAML::Key << "FixedRotation" << YAML::Value << rb.FixedRotation;
            out << YAML::EndMap;
        }

        // BoxColliderComponent
        if (entity.HasComponent<BoxColliderComponent>())
        {
            out << YAML::Key << "BoxColliderComponent";
            out << YAML::BeginMap;
            auto& bc = entity.GetComponent<BoxColliderComponent>();
            out << YAML::Key << "HalfExtents" << YAML::Value << bc.HalfExtents;
            out << YAML::Key << "Offset" << YAML::Value << bc.Offset;
            out << YAML::EndMap;
        }

        // SphereColliderComponent
        if (entity.HasComponent<SphereColliderComponent>())
        {
            out << YAML::Key << "SphereColliderComponent";
            out << YAML::BeginMap;
            auto& sc = entity.GetComponent<SphereColliderComponent>();
            out << YAML::Key << "Radius" << YAML::Value << sc.Radius;
            out << YAML::Key << "Offset" << YAML::Value << sc.Offset;
            out << YAML::EndMap;
        }

        out << YAML::EndMap;
    }

    bool SceneSerializer::Serialize(const std::string& filepath, const EditorRenderSettings& renderSettings)
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Scene" << YAML::Value << "Untitled";

        // Shadow settings
        {
            auto& shadow = m_Scene->GetShadowSettings();
            out << YAML::Key << "ShadowSettings";
            out << YAML::BeginMap;
            out << YAML::Key << "Enabled" << YAML::Value << shadow.Enabled;
            out << YAML::Key << "MapResolution" << YAML::Value << shadow.MapResolution;
            out << YAML::Key << "Bias" << YAML::Value << shadow.Bias;
            out << YAML::Key << "OrthoSize" << YAML::Value << shadow.OrthoSize;
            out << YAML::Key << "NearPlane" << YAML::Value << shadow.NearPlane;
            out << YAML::Key << "FarPlane" << YAML::Value << shadow.FarPlane;
            out << YAML::EndMap;
        }

        // Render settings (后处理 + MSAA + 物理后端)
        {
            out << YAML::Key << "RenderSettings";
            out << YAML::BeginMap;
            out << YAML::Key << "BloomEnabled" << YAML::Value << renderSettings.PostProcessing.BloomEnabled;
            out << YAML::Key << "BloomThreshold" << YAML::Value << renderSettings.PostProcessing.BloomThreshold;
            out << YAML::Key << "BloomStrength" << YAML::Value << renderSettings.PostProcessing.BloomStrength;
            out << YAML::Key << "BloomIterations" << YAML::Value << renderSettings.PostProcessing.BloomIterations;
            out << YAML::Key << "ToneMappingMode" << YAML::Value << renderSettings.PostProcessing.ToneMappingMode;
            out << YAML::Key << "GammaCorrection" << YAML::Value << renderSettings.PostProcessing.GammaCorrection;
            out << YAML::Key << "MSAASamples" << YAML::Value << renderSettings.MSAASamples;
            out << YAML::Key << "PhysicsBackend" << YAML::Value << renderSettings.PhysicsBackend;
            out << YAML::EndMap;
        }

        // Skybox settings
        {
            const auto& skyboxPaths = m_Scene->GetSkyboxFacePaths();
            if (!skyboxPaths.empty())
            {
                out << YAML::Key << "Skybox";
                out << YAML::BeginMap;
                out << YAML::Key << "Faces" << YAML::Value << YAML::Flow << skyboxPaths;
                out << YAML::EndMap;
            }
        }

        out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

        // All entities have IDComponent, so iterate via view
        auto view = m_Scene->GetAllEntitiesWith<IDComponent>();
        for (auto entityID : view)
        {
            Entity entity = {entityID, m_Scene.get()};
            if (!entity)
                continue;

            SerializeEntity(out, entity);
        }

        out << YAML::EndSeq;
        out << YAML::EndMap;

        std::ofstream fout(filepath);
        if (!fout.is_open())
        {
            ENGINE_CORE_ERROR("Could not open file '{0}' for writing", filepath);
            return false;
        }
        fout << out.c_str();
        if (!fout.good())
        {
            ENGINE_CORE_ERROR("Failed to write scene file '{0}'", filepath);
            return false;
        }
        return true;
    }

    static Ref<Mesh> CreateMeshFromType(const std::string& meshType, const std::string& modelPath = "")
    {
        if (meshType == "Cube")
            return Mesh::CreateCube();
        if (meshType == "Plane")
            return Mesh::CreatePlane();
        if (meshType == "Sphere")
            return Mesh::CreateSphere();
        if (meshType == "Model" && !modelPath.empty())
        {
            auto mesh = Mesh::CreateFromFile(modelPath);
            if (mesh)
                return mesh;
            ENGINE_CORE_WARN("Failed to load model '{0}', falling back to Cube", modelPath);
        }
        return Mesh::CreateCube();
    }

    bool SceneSerializer::Deserialize(const std::string& filepath, EditorRenderSettings* outRenderSettings)
    {
        YAML::Node data;
        try
        {
            data = YAML::LoadFile(filepath);
        }
        catch (const YAML::ParserException& e)
        {
            ENGINE_CORE_ERROR("Failed to parse scene file '{0}': {1}", filepath, e.what());
            return false;
        }
        catch (const YAML::BadFile& e)
        {
            ENGINE_CORE_ERROR("Failed to open scene file '{0}': {1}", filepath, e.what());
            return false;
        }

        if (!data["Scene"])
            return false;

        // Deserialize shadow settings
        auto shadowNode = data["ShadowSettings"];
        if (shadowNode)
        {
            auto& shadow = m_Scene->GetShadowSettings();
            if (shadowNode["Enabled"])
                shadow.Enabled = shadowNode["Enabled"].as<bool>();
            if (shadowNode["MapResolution"])
            {
                int res = shadowNode["MapResolution"].as<int>();
                // 范围校验：256 ~ 8192
                if (res < 256) res = 256;
                if (res > 8192) res = 8192;
                m_Scene->ResizeShadowMap(res);
            }
            if (shadowNode["Bias"])
                shadow.Bias = shadowNode["Bias"].as<float>();
            if (shadowNode["OrthoSize"])
                shadow.OrthoSize = shadowNode["OrthoSize"].as<float>();
            if (shadowNode["NearPlane"])
                shadow.NearPlane = shadowNode["NearPlane"].as<float>();
            if (shadowNode["FarPlane"])
                shadow.FarPlane = shadowNode["FarPlane"].as<float>();
        }

        // Deserialize render settings
        auto renderNode = data["RenderSettings"];
        if (renderNode && outRenderSettings)
        {
            if (renderNode["BloomEnabled"])
                outRenderSettings->PostProcessing.BloomEnabled = renderNode["BloomEnabled"].as<bool>();
            if (renderNode["BloomThreshold"])
                outRenderSettings->PostProcessing.BloomThreshold = renderNode["BloomThreshold"].as<float>();
            if (renderNode["BloomStrength"])
                outRenderSettings->PostProcessing.BloomStrength = renderNode["BloomStrength"].as<float>();
            if (renderNode["BloomIterations"])
            {
                int iters = renderNode["BloomIterations"].as<int>();
                if (iters < 1) iters = 1;
                if (iters > 20) iters = 20;
                outRenderSettings->PostProcessing.BloomIterations = iters;
            }
            if (renderNode["ToneMappingMode"])
            {
                int mode = renderNode["ToneMappingMode"].as<int>();
                if (mode < 0 || mode > 3)
                {
                    ENGINE_CORE_WARN("Invalid ToneMappingMode {0}, falling back to 0", mode);
                    mode = 0;
                }
                outRenderSettings->PostProcessing.ToneMappingMode = mode;
            }
            if (renderNode["GammaCorrection"])
                outRenderSettings->PostProcessing.GammaCorrection = renderNode["GammaCorrection"].as<bool>();
            if (renderNode["MSAASamples"])
            {
                uint32_t samples = renderNode["MSAASamples"].as<uint32_t>();
                // 白名单校验：仅允许 1, 2, 4, 8
                if (samples != 1 && samples != 2 && samples != 4 && samples != 8)
                {
                    ENGINE_CORE_WARN("Invalid MSAASamples {0}, falling back to 1", samples);
                    samples = 1;
                }
                outRenderSettings->MSAASamples = samples;
            }
            if (renderNode["PhysicsBackend"])
            {
                int backend = renderNode["PhysicsBackend"].as<int>();
                if (backend < 0 || backend > 1)
                {
                    ENGINE_CORE_WARN("Invalid PhysicsBackend {0}, falling back to 0 (Custom)", backend);
                    backend = 0;
                }
                outRenderSettings->PhysicsBackend = backend;
            }
        }

        // Deserialize skybox
        auto skyboxNode = data["Skybox"];
        if (skyboxNode && skyboxNode["Faces"])
        {
            auto facesNode = skyboxNode["Faces"];
            if (facesNode.IsSequence() && facesNode.size() == 6)
            {
                std::vector<std::string> faces;
                for (const auto& f : facesNode)
                    faces.push_back(f.as<std::string>());
                m_Scene->LoadSkybox(faces);
            }
        }

        auto entities = data["Entities"];
        if (!entities)
            return true;

        for (auto entityNode : entities)
        {
            Entity deserializedEntity;
            try
            {
                uint64_t uuid = entityNode["Entity"].as<uint64_t>();

                std::string name;
                auto tagComponent = entityNode["TagComponent"];
                if (tagComponent)
                    name = tagComponent["Tag"].as<std::string>();

                deserializedEntity = m_Scene->CreateEntityWithUUID(UUID(uuid), name);

                // TransformComponent
                auto transformComponent = entityNode["TransformComponent"];
                if (transformComponent)
                {
                    auto& tc = deserializedEntity.GetComponent<TransformComponent>();
                    tc.Translation = transformComponent["Translation"].as<glm::vec3>();
                    tc.Rotation = transformComponent["Rotation"].as<glm::vec3>();
                    tc.Scale = transformComponent["Scale"].as<glm::vec3>();
                }

                // MeshRendererComponent
                auto meshRendererComponent = entityNode["MeshRendererComponent"];
                if (meshRendererComponent)
                {
                    auto& mrc = deserializedEntity.AddComponent<MeshRendererComponent>();
                    std::string meshType = meshRendererComponent["MeshType"].as<std::string>();

                    // Read model path for Model type
                    std::string modelPath;
                    if (meshType == "Model" && meshRendererComponent["ModelPath"])
                    {
                        modelPath = meshRendererComponent["ModelPath"].as<std::string>();
                        // Security: reject unsafe paths
                        if (!IsSafeAssetPath(modelPath))
                        {
                            ENGINE_CORE_WARN("Rejected unsafe model path: {0}", modelPath);
                            modelPath.clear();
                        }
                    }

                    mrc.MeshData = CreateMeshFromType(meshType, modelPath);
                    mrc.ModelPath = modelPath;
                    mrc.Color = meshRendererComponent["Color"].as<glm::vec4>();

                    if (meshRendererComponent["TexturePath"])
                    {
                        std::string texPath = meshRendererComponent["TexturePath"].as<std::string>();
                        if (IsSafeAssetPath(texPath))
                        {
                            mrc.TexturePath = texPath;
                            mrc.DiffuseTexture = Texture2D::Create(mrc.TexturePath);
                        }
                        else if (!texPath.empty())
                        {
                            ENGINE_CORE_WARN("Rejected unsafe texture path: {0}", texPath);
                        }
                    }
                    if (meshRendererComponent["Tiling"])
                        mrc.Tiling = meshRendererComponent["Tiling"].as<glm::vec2>();
                    if (meshRendererComponent["Shininess"])
                        mrc.Shininess = meshRendererComponent["Shininess"].as<float>();

                    if (meshRendererComponent["NormalMapPath"])
                    {
                        std::string normalPath = meshRendererComponent["NormalMapPath"].as<std::string>();
                        if (IsSafeAssetPath(normalPath))
                        {
                            mrc.NormalMapPath = normalPath;
                            mrc.NormalMapTexture = Texture2D::Create(mrc.NormalMapPath);
                        }
                        else if (!normalPath.empty())
                        {
                            ENGINE_CORE_WARN("Rejected unsafe normal map path: {0}", normalPath);
                        }
                    }

                    // PBR parameters
                    if (meshRendererComponent["Metallic"])
                        mrc.Metallic = meshRendererComponent["Metallic"].as<float>();
                    if (meshRendererComponent["Roughness"])
                        mrc.Roughness = meshRendererComponent["Roughness"].as<float>();

                    auto loadSafeTexture = [](const YAML::Node& node, const std::string& key,
                                              std::string& outPath, Ref<Texture2D>& outTex) {
                        if (!node[key]) return;
                        std::string path = node[key].as<std::string>();
                        if (IsSafeAssetPath(path))
                        {
                            outPath = path;
                            outTex = Texture2D::Create(path);
                        }
                        else if (!path.empty())
                        {
                            ENGINE_CORE_WARN("Rejected unsafe texture path: {0}", path);
                        }
                    };

                    loadSafeTexture(meshRendererComponent, "MetallicTexturePath",
                                    mrc.MetallicTexturePath, mrc.MetallicTexture);
                    loadSafeTexture(meshRendererComponent, "RoughnessTexturePath",
                                    mrc.RoughnessTexturePath, mrc.RoughnessTexture);
                    loadSafeTexture(meshRendererComponent, "AOTexturePath",
                                    mrc.AOTexturePath, mrc.AOTexture);
                }

                // LightComponent
                auto lightComponent = entityNode["LightComponent"];
                if (lightComponent)
                {
                    auto& lc = deserializedEntity.AddComponent<LightComponent>();
                    int typeVal = lightComponent["Type"] ? lightComponent["Type"].as<int>() : 0;
                    if (typeVal < 0 || typeVal > 2)
                        typeVal = 0;
                    lc.Type = static_cast<LightComponent::LightType>(typeVal);
                    if (lightComponent["Color"])
                        lc.Color = lightComponent["Color"].as<glm::vec3>();
                    if (lightComponent["Intensity"])
                        lc.Intensity = lightComponent["Intensity"].as<float>();
                    if (lightComponent["Constant"])
                        lc.Constant = lightComponent["Constant"].as<float>();
                    if (lightComponent["Linear"])
                        lc.Linear = lightComponent["Linear"].as<float>();
                    if (lightComponent["Quadratic"])
                        lc.Quadratic = lightComponent["Quadratic"].as<float>();
                    if (lightComponent["InnerCutoff"])
                        lc.InnerCutoff = lightComponent["InnerCutoff"].as<float>();
                    if (lightComponent["OuterCutoff"])
                        lc.OuterCutoff = lightComponent["OuterCutoff"].as<float>();
                    if (lightComponent["CastShadows"])
                        lc.CastShadows = lightComponent["CastShadows"].as<bool>();
                }

                // CameraComponent
                auto cameraComponent = entityNode["CameraComponent"];
                if (cameraComponent)
                {
                    auto& cc = deserializedEntity.AddComponent<CameraComponent>();

                    if (cameraComponent["ProjectionType"])
                        cc.Camera.SetProjectionType(
                            static_cast<SceneCamera::ProjectionType>(cameraComponent["ProjectionType"].as<int>()));
                    if (cameraComponent["PerspectiveFOV"])
                        cc.Camera.SetPerspectiveVerticalFOV(cameraComponent["PerspectiveFOV"].as<float>());
                    if (cameraComponent["PerspectiveNear"])
                        cc.Camera.SetPerspectiveNearClip(cameraComponent["PerspectiveNear"].as<float>());
                    if (cameraComponent["PerspectiveFar"])
                        cc.Camera.SetPerspectiveFarClip(cameraComponent["PerspectiveFar"].as<float>());
                    if (cameraComponent["OrthographicSize"])
                        cc.Camera.SetOrthographicSize(cameraComponent["OrthographicSize"].as<float>());
                    if (cameraComponent["OrthographicNear"])
                        cc.Camera.SetOrthographicNearClip(cameraComponent["OrthographicNear"].as<float>());
                    if (cameraComponent["OrthographicFar"])
                        cc.Camera.SetOrthographicFarClip(cameraComponent["OrthographicFar"].as<float>());
                    if (cameraComponent["Primary"])
                        cc.Primary = cameraComponent["Primary"].as<bool>();
                    if (cameraComponent["FixedAspectRatio"])
                        cc.FixedAspectRatio = cameraComponent["FixedAspectRatio"].as<bool>();
                }

                // RigidBodyComponent
                auto rigidBodyComponent = entityNode["RigidBodyComponent"];
                if (rigidBodyComponent)
                {
                    auto& rb = deserializedEntity.AddComponent<RigidBodyComponent>();
                    int typeVal = rigidBodyComponent["Type"] ? rigidBodyComponent["Type"].as<int>() : 0;
                    if (typeVal < 0 || typeVal > 2) typeVal = 0;
                    rb.Type = static_cast<RigidBodyComponent::BodyType>(typeVal);
                    if (rigidBodyComponent["Mass"])
                        rb.Mass = rigidBodyComponent["Mass"].as<float>();
                    if (rigidBodyComponent["Restitution"])
                        rb.Restitution = rigidBodyComponent["Restitution"].as<float>();
                    if (rigidBodyComponent["Friction"])
                        rb.Friction = rigidBodyComponent["Friction"].as<float>();
                    if (rigidBodyComponent["GravityScale"])
                        rb.GravityScale = rigidBodyComponent["GravityScale"].as<float>();
                    if (rigidBodyComponent["FixedRotation"])
                        rb.FixedRotation = rigidBodyComponent["FixedRotation"].as<bool>();
                }

                // BoxColliderComponent
                auto boxColliderComponent = entityNode["BoxColliderComponent"];
                if (boxColliderComponent)
                {
                    auto& bc = deserializedEntity.AddComponent<BoxColliderComponent>();
                    if (boxColliderComponent["HalfExtents"])
                        bc.HalfExtents = boxColliderComponent["HalfExtents"].as<glm::vec3>();
                    if (boxColliderComponent["Offset"])
                        bc.Offset = boxColliderComponent["Offset"].as<glm::vec3>();
                }

                // SphereColliderComponent
                auto sphereColliderComponent = entityNode["SphereColliderComponent"];
                if (sphereColliderComponent)
                {
                    auto& sc = deserializedEntity.AddComponent<SphereColliderComponent>();
                    if (sphereColliderComponent["Radius"])
                        sc.Radius = sphereColliderComponent["Radius"].as<float>();
                    if (sphereColliderComponent["Offset"])
                        sc.Offset = sphereColliderComponent["Offset"].as<glm::vec3>();
                }
            }
            catch (const YAML::Exception& e)
            {
                ENGINE_CORE_ERROR("Failed to deserialize entity: {0}", e.what());
                // Rollback: destroy partially-created entity to avoid corrupt scene state
                if (deserializedEntity)
                    m_Scene->DestroyEntity(deserializedEntity);
                continue;
            }
        }

        return true;
    }

} // namespace Engine
