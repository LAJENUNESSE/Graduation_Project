#include "engpch.h"
#include "Scene/SceneSerializer.h"

#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Script/NativeScriptComponent.h"
#include "Script/ScriptRegistry.h"
#include "Reflection/ComponentRegistry.h"
#include "Reflection/AutoSerializer.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Asset/AssetManager.h"
#include "Asset/PathUtils.h"
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

        // RelationshipComponent（父子层级）
        if (entity.HasComponent<RelationshipComponent>())
        {
            auto& rel = entity.GetComponent<RelationshipComponent>();
            if (static_cast<uint64_t>(rel.ParentID) != 0)
            {
                out << YAML::Key << "ParentID" << YAML::Value << static_cast<uint64_t>(rel.ParentID);
            }
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

            // MeshType
            const char* meshTypeStr = "Cube";
            switch (mrc.Type)
            {
            case MeshType::Cube:   meshTypeStr = "Cube";   break;
            case MeshType::Plane:  meshTypeStr = "Plane";  break;
            case MeshType::Sphere: meshTypeStr = "Sphere"; break;
            case MeshType::Model:  meshTypeStr = "Model";  break;
            }
            out << YAML::Key << "MeshType" << YAML::Value << meshTypeStr;

            if (mrc.Type == MeshType::Model)
            {
                const std::string& modelPath = AssetManager::GetPath<Mesh>(mrc.MeshAsset);
                out << YAML::Key << "ModelPath" << YAML::Value << modelPath;
            }

            out << YAML::Key << "Color" << YAML::Value << mrc.Color;
            out << YAML::Key << "TexturePath" << YAML::Value << AssetManager::GetPath<Texture2D>(mrc.DiffuseTextureAsset);
            out << YAML::Key << "Tiling" << YAML::Value << mrc.Tiling;
            out << YAML::Key << "Shininess" << YAML::Value << mrc.Shininess;
            out << YAML::Key << "NormalMapPath" << YAML::Value << AssetManager::GetPath<Texture2D>(mrc.NormalMapAsset);
            out << YAML::Key << "Metallic" << YAML::Value << mrc.Metallic;
            out << YAML::Key << "Roughness" << YAML::Value << mrc.Roughness;
            out << YAML::Key << "MetallicTexturePath" << YAML::Value << AssetManager::GetPath<Texture2D>(mrc.MetallicTextureAsset);
            out << YAML::Key << "RoughnessTexturePath" << YAML::Value << AssetManager::GetPath<Texture2D>(mrc.RoughnessTextureAsset);
            out << YAML::Key << "AOTexturePath" << YAML::Value << AssetManager::GetPath<Texture2D>(mrc.AOTextureAsset);
            out << YAML::EndMap;
        }

        // CameraComponent（手写：使用 getter/setter）
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

        // LightComponent, RigidBodyComponent, BoxColliderComponent,
        // SphereColliderComponent, CollisionParticleTriggerComponent
        // 统一通过反射系统自动序列化
        for (auto& meta : ComponentRegistry::Instance().GetAll())
        {
            uint32_t entityId = static_cast<uint32_t>(static_cast<entt::entity>(entity));
            auto* scene = entity.GetScene();
            if (scene && meta.Has(*scene, entityId))
            {
                void* comp = meta.Get(*scene, entityId);
                out << YAML::Key << meta.TypeName;
                out << YAML::BeginMap;
                AutoSerializer::Serialize(out, meta, comp);
                out << YAML::EndMap;
            }
        }

        // NativeScriptComponent
        if (entity.HasComponent<NativeScriptComponent>())
        {
            out << YAML::Key << "NativeScriptComponent";
            out << YAML::BeginMap;
            auto& nsc = entity.GetComponent<NativeScriptComponent>();
            out << YAML::Key << "ScriptName" << YAML::Value << nsc.ScriptName;
            out << YAML::EndMap;
        }

        // TerrainComponent
        if (entity.HasComponent<TerrainComponent>())
        {
            out << YAML::Key << "TerrainComponent" << YAML::BeginMap;
            auto& tc = entity.GetComponent<TerrainComponent>();
            out << YAML::Key << "HeightmapPath" << YAML::Value << tc.HeightmapPath;
            out << YAML::Key << "HeightScale" << YAML::Value << tc.HeightScale;
            out << YAML::Key << "TerrainSize" << YAML::Value << tc.TerrainSize;
            out << YAML::Key << "SplatmapPath" << YAML::Value << tc.SplatmapPath;
            for (int i = 0; i < 4; i++)
            {
                std::string p = "Layer" + std::to_string(i);
                out << YAML::Key << (p + "Texture") << YAML::Value << AssetManager::GetPath<Texture2D>(tc.LayerTextures[i]);
                out << YAML::Key << (p + "NormalMap") << YAML::Value << AssetManager::GetPath<Texture2D>(tc.LayerNormalMaps[i]);
                out << YAML::Key << (p + "Tiling") << YAML::Value << tc.LayerTiling[i];
                out << YAML::Key << (p + "Metallic") << YAML::Value << tc.LayerMetallic[i];
                out << YAML::Key << (p + "Roughness") << YAML::Value << tc.LayerRoughness[i];
            }
            out << YAML::Key << "Friction" << YAML::Value << tc.Friction;
            out << YAML::Key << "Restitution" << YAML::Value << tc.Restitution;
            out << YAML::Key << "LODLevels" << YAML::Value << tc.LODLevels;
            out << YAML::Key << "LODDistance1" << YAML::Value << tc.LODDistance1;
            out << YAML::Key << "LODDistance2" << YAML::Value << tc.LODDistance2;
            out << YAML::Key << "GrassEnabled" << YAML::Value << tc.GrassEnabled;
            out << YAML::Key << "GrassDensity" << YAML::Value << tc.GrassDensity;
            out << YAML::Key << "GrassHeight" << YAML::Value << tc.GrassHeight;
            out << YAML::Key << "GrassWidth" << YAML::Value << tc.GrassWidth;
            out << YAML::Key << "GrassWindStrength" << YAML::Value << tc.GrassWindStrength;
            out << YAML::Key << "GrassTexture" << YAML::Value << AssetManager::GetPath<Texture2D>(tc.GrassTexture);
            out << YAML::EndMap;
        }

        // AudioSourceComponent
        if (entity.HasComponent<AudioSourceComponent>())
        {
            out << YAML::Key << "AudioSourceComponent";
            out << YAML::BeginMap;
            auto& asc = entity.GetComponent<AudioSourceComponent>();
            out << YAML::Key << "AudioPath" << YAML::Value << asc.AudioPath;
            out << YAML::Key << "Volume" << YAML::Value << asc.Volume;
            out << YAML::Key << "Pitch" << YAML::Value << asc.Pitch;
            out << YAML::Key << "MinDistance" << YAML::Value << asc.MinDistance;
            out << YAML::Key << "MaxDistance" << YAML::Value << asc.MaxDistance;
            out << YAML::Key << "Loop" << YAML::Value << asc.Loop;
            out << YAML::Key << "PlayOnStart" << YAML::Value << asc.PlayOnStart;
            out << YAML::Key << "Spatial" << YAML::Value << asc.Spatial;
            out << YAML::EndMap;
        }

        // AudioListenerComponent
        if (entity.HasComponent<AudioListenerComponent>())
        {
            out << YAML::Key << "AudioListenerComponent";
            out << YAML::BeginMap;
            auto& alc = entity.GetComponent<AudioListenerComponent>();
            out << YAML::Key << "Active" << YAML::Value << alc.Active;
            out << YAML::EndMap;
        }

        // VideoPlayerComponent
        if (entity.HasComponent<VideoPlayerComponent>())
        {
            out << YAML::Key << "VideoPlayerComponent";
            out << YAML::BeginMap;
            auto& vpc = entity.GetComponent<VideoPlayerComponent>();
            out << YAML::Key << "StreamURL" << YAML::Value << vpc.StreamURL;
            out << YAML::Key << "PlayOnStart" << YAML::Value << vpc.PlayOnStart;
            out << YAML::Key << "Loop" << YAML::Value << vpc.Loop;
            out << YAML::Key << "Volume" << YAML::Value << vpc.Volume;
            out << YAML::EndMap;
        }

        // ParticleEmitterComponent
        if (entity.HasComponent<ParticleEmitterComponent>())
        {
            out << YAML::Key << "ParticleEmitterComponent";
            out << YAML::BeginMap;
            auto& pe = entity.GetComponent<ParticleEmitterComponent>();
            out << YAML::Key << "Preset" << YAML::Value << static_cast<int>(pe.CurrentPreset);
            out << YAML::Key << "EmitRate" << YAML::Value << pe.EmitRate;
            out << YAML::Key << "BurstCount" << YAML::Value << pe.BurstCount;
            out << YAML::Key << "MaxParticles" << YAML::Value << pe.MaxParticles;
            out << YAML::Key << "LifeMin" << YAML::Value << pe.LifeMin;
            out << YAML::Key << "LifeMax" << YAML::Value << pe.LifeMax;
            out << YAML::Key << "SpeedMin" << YAML::Value << pe.SpeedMin;
            out << YAML::Key << "SpeedMax" << YAML::Value << pe.SpeedMax;
            out << YAML::Key << "SizeStart" << YAML::Value << pe.SizeStart;
            out << YAML::Key << "SizeEnd" << YAML::Value << pe.SizeEnd;
            out << YAML::Key << "EmitDirection" << YAML::Value << pe.EmitDirection;
            out << YAML::Key << "EmitAngle" << YAML::Value << pe.EmitAngle;
            out << YAML::Key << "ColorStart" << YAML::Value << pe.ColorStart;
            out << YAML::Key << "ColorEnd" << YAML::Value << pe.ColorEnd;
            out << YAML::Key << "Gravity" << YAML::Value << pe.Gravity;
            out << YAML::Key << "Damping" << YAML::Value << pe.Damping;
            out << YAML::Key << "BlendMode" << YAML::Value << static_cast<int>(pe.Blend);
            out << YAML::Key << "SPHEnabled" << YAML::Value << pe.SPHEnabled;
            out << YAML::Key << "SPH_RestDensity" << YAML::Value << pe.SPH_RestDensity;
            out << YAML::Key << "SPH_GasConstant" << YAML::Value << pe.SPH_GasConstant;
            out << YAML::Key << "SPH_Viscosity" << YAML::Value << pe.SPH_Viscosity;
            out << YAML::Key << "SPH_SmoothingRadius" << YAML::Value << pe.SPH_SmoothingRadius;
            out << YAML::Key << "SPH_ParticleMass" << YAML::Value << pe.SPH_ParticleMass;
            out << YAML::Key << "SPH_PCISPHEnabled" << YAML::Value << pe.SPH_PCISPHEnabled;
            out << YAML::Key << "SPH_PCISPHIterations" << YAML::Value << pe.SPH_PCISPHIterations;
            out << YAML::Key << "SPH_PCISPHDelta" << YAML::Value << pe.SPH_PCISPHDelta;
            out << YAML::Key << "SPH_SurfaceTension" << YAML::Value << pe.SPH_SurfaceTension;
            out << YAML::Key << "SPH_RigidBodyCoupling" << YAML::Value << pe.SPH_RigidBodyCoupling;
            out << YAML::Key << "SPH_BoundaryStiffness" << YAML::Value << pe.SPH_BoundaryStiffness;
            out << YAML::Key << "SPH_BoundaryDamping" << YAML::Value << pe.SPH_BoundaryDamping;
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
            out << YAML::Key << "CSMEnabled" << YAML::Value << shadow.CSMEnabled;
            out << YAML::Key << "CascadeCount" << YAML::Value << shadow.CascadeCount;
            out << YAML::Key << "CascadeSplitLambda" << YAML::Value << shadow.CascadeSplitLambda;
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
            out << YAML::Key << "SSAOEnabled" << YAML::Value << renderSettings.SSAOEnabled;
            out << YAML::Key << "SSAORadius" << YAML::Value << renderSettings.SSAORadius;
            out << YAML::Key << "SSAOBias" << YAML::Value << renderSettings.SSAOBias;
            out << YAML::Key << "SSAOKernelSize" << YAML::Value << renderSettings.SSAOKernelSize;
            out << YAML::Key << "SSAOIntensity" << YAML::Value << renderSettings.SSAOIntensity;
            out << YAML::EndMap;
        }

        // Skybox settings
        {
            const auto& skyboxPaths = m_Scene->GetSkyboxFacePaths();
            if (!skyboxPaths.empty())
            {                std::vector<std::string> relativePaths;
                for (const auto& p : skyboxPaths)
                    relativePaths.push_back(PathUtils::ToProjectRelativeOrAbsolute(p));

                out << YAML::Key << "Skybox";
                out << YAML::BeginMap;
                out << YAML::Key << "Faces" << YAML::Value << YAML::Flow << relativePaths;
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

        const std::filesystem::path resolvedFilepath = PathUtils::ResolvePath(filepath);
        std::error_code ec;
        std::filesystem::create_directories(resolvedFilepath.parent_path(), ec);
        std::ofstream fout(resolvedFilepath);
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

    static AssetHandle CreateMeshHandleFromType(const std::string& meshType, const std::string& modelPath = "")
    {
        if (meshType == "Cube")
            return AssetManager::Load<Mesh>("builtin:Cube");
        if (meshType == "Plane")
            return AssetManager::Load<Mesh>("builtin:Plane");
        if (meshType == "Sphere")
            return AssetManager::Load<Mesh>("builtin:Sphere");
        if (meshType == "Model" && !modelPath.empty())
        {
            auto h = AssetManager::Load<Mesh>(modelPath);
            if (h.IsValid())
                return h;
            ENGINE_CORE_WARN("Failed to load model '{0}', falling back to Cube", modelPath);
        }
        return AssetManager::Load<Mesh>("builtin:Cube");
    }

    static MeshType MeshTypeFromString(const std::string& str)
    {
        if (str == "Plane")  return MeshType::Plane;
        if (str == "Sphere") return MeshType::Sphere;
        if (str == "Model")  return MeshType::Model;
        return MeshType::Cube;
    }

    bool SceneSerializer::Deserialize(const std::string& filepath, EditorRenderSettings* outRenderSettings)
    {
        YAML::Node data;
        try
        {
            const std::filesystem::path resolvedFilepath = PathUtils::ResolvePath(filepath);
            data = YAML::LoadFile(resolvedFilepath.string());
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
            if (shadowNode["CSMEnabled"])
                shadow.CSMEnabled = shadowNode["CSMEnabled"].as<bool>();
            if (shadowNode["CascadeCount"])
                shadow.CascadeCount = std::clamp(shadowNode["CascadeCount"].as<int>(), 1, 4);
            if (shadowNode["CascadeSplitLambda"])
                shadow.CascadeSplitLambda = shadowNode["CascadeSplitLambda"].as<float>();
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
            if (renderNode["SSAOEnabled"])
                outRenderSettings->SSAOEnabled = renderNode["SSAOEnabled"].as<bool>();
            if (renderNode["SSAORadius"])
                outRenderSettings->SSAORadius = renderNode["SSAORadius"].as<float>();
            if (renderNode["SSAOBias"])
                outRenderSettings->SSAOBias = renderNode["SSAOBias"].as<float>();
            if (renderNode["SSAOKernelSize"])
                outRenderSettings->SSAOKernelSize = std::clamp(renderNode["SSAOKernelSize"].as<int>(), 4, 64);
            if (renderNode["SSAOIntensity"])
                outRenderSettings->SSAOIntensity = renderNode["SSAOIntensity"].as<float>();
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
                uint64_t uuid = entityNode["Entity"]
                    ? entityNode["Entity"].as<uint64_t>()
                    : static_cast<uint64_t>(UUID());

                std::string name;
                auto tagComponent = entityNode["TagComponent"];
                if (tagComponent && tagComponent["Tag"])
                    name = tagComponent["Tag"].as<std::string>();

                deserializedEntity = m_Scene->CreateEntityWithUUID(UUID(uuid), name);

                // ParentID（父子层级，向后兼容：无该字段默认为根节点）
                if (entityNode["ParentID"])
                {
                    auto& rel = deserializedEntity.GetComponent<RelationshipComponent>();
                    rel.ParentID = UUID(entityNode["ParentID"].as<uint64_t>());
                }

                // TransformComponent
                auto transformComponent = entityNode["TransformComponent"];
                if (transformComponent)
                {
                    auto& tc = deserializedEntity.GetComponent<TransformComponent>();
                    if (transformComponent["Translation"])
                        tc.Translation = transformComponent["Translation"].as<glm::vec3>();
                    if (transformComponent["Rotation"])
                        tc.Rotation = transformComponent["Rotation"].as<glm::vec3>();
                    if (transformComponent["Scale"])
                        tc.Scale = transformComponent["Scale"].as<glm::vec3>();
                }

                // MeshRendererComponent
                auto meshRendererComponent = entityNode["MeshRendererComponent"];
                if (meshRendererComponent)
                {
                    auto& mrc = deserializedEntity.AddComponent<MeshRendererComponent>();
                    std::string meshType = meshRendererComponent["MeshType"]
                        ? meshRendererComponent["MeshType"].as<std::string>() : "Cube";

                    // Read model path for Model type
                    std::string modelPath;
                    if (meshType == "Model" && meshRendererComponent["ModelPath"])
                    {
                        modelPath = meshRendererComponent["ModelPath"].as<std::string>();
                        if (!PathUtils::IsSafeAssetPath(modelPath))
                        {
                            ENGINE_CORE_WARN("Rejected unsafe model path: {0}", modelPath);
                            modelPath.clear();
                        }
                    }

                    mrc.Type = MeshTypeFromString(meshType);
                    mrc.MeshAsset = CreateMeshHandleFromType(meshType, modelPath);
                    if (meshRendererComponent["Color"])
                        mrc.Color = meshRendererComponent["Color"].as<glm::vec4>();

                    if (meshRendererComponent["TexturePath"])
                    {
                        std::string texPath = meshRendererComponent["TexturePath"].as<std::string>();
                        if (PathUtils::IsSafeAssetPath(texPath))
                            mrc.DiffuseTextureAsset = AssetManager::Load<Texture2D>(texPath);
                        else if (!texPath.empty())
                            ENGINE_CORE_WARN("Rejected unsafe texture path: {0}", texPath);
                    }
                    if (meshRendererComponent["Tiling"])
                        mrc.Tiling = meshRendererComponent["Tiling"].as<glm::vec2>();
                    if (meshRendererComponent["Shininess"])
                        mrc.Shininess = meshRendererComponent["Shininess"].as<float>();

                    if (meshRendererComponent["NormalMapPath"])
                    {
                        std::string normalPath = meshRendererComponent["NormalMapPath"].as<std::string>();
                        if (PathUtils::IsSafeAssetPath(normalPath))
                            mrc.NormalMapAsset = AssetManager::Load<Texture2D>(normalPath);
                        else if (!normalPath.empty())
                            ENGINE_CORE_WARN("Rejected unsafe normal map path: {0}", normalPath);
                    }

                    // PBR parameters
                    if (meshRendererComponent["Metallic"])
                        mrc.Metallic = meshRendererComponent["Metallic"].as<float>();
                    if (meshRendererComponent["Roughness"])
                        mrc.Roughness = meshRendererComponent["Roughness"].as<float>();

                    auto loadSafeTextureHandle = [](const YAML::Node& node, const std::string& key) -> AssetHandle {
                        if (!node[key]) return {};
                        std::string path = node[key].as<std::string>();
                        if (PathUtils::IsSafeAssetPath(path))
                            return AssetManager::Load<Texture2D>(path);
                        if (!path.empty())
                            ENGINE_CORE_WARN("Rejected unsafe texture path: {0}", path);
                        return {};
                    };

                    mrc.MetallicTextureAsset = loadSafeTextureHandle(meshRendererComponent, "MetallicTexturePath");
                    mrc.RoughnessTextureAsset = loadSafeTextureHandle(meshRendererComponent, "RoughnessTexturePath");
                    mrc.AOTextureAsset = loadSafeTextureHandle(meshRendererComponent, "AOTexturePath");
                }

                // CameraComponent（手写：getter/setter API）
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

                // 反射组件自动反序列化
                for (auto& meta : ComponentRegistry::Instance().GetAll())
                {
                    auto compNode = entityNode[meta.TypeName];
                    if (compNode)
                    {
                        uint32_t entityId = static_cast<uint32_t>(static_cast<entt::entity>(deserializedEntity));
                        meta.Add(*m_Scene, entityId);
                        void* comp = meta.Get(*m_Scene, entityId);
                        AutoSerializer::Deserialize(compNode, meta, comp);
                    }
                }

                // TerrainComponent
                auto terrainNode = entityNode["TerrainComponent"];
                if (terrainNode)
                {
                    auto& tc = deserializedEntity.AddComponent<TerrainComponent>();

                    auto loadSafePath = [](const YAML::Node& n, const std::string& k) -> std::string {
                        if (!n[k]) return "";
                        std::string p = n[k].as<std::string>();
                        return PathUtils::IsSafeAssetPath(p) ? p : "";
                    };

                    tc.HeightmapPath = loadSafePath(terrainNode, "HeightmapPath");
                    if (terrainNode["HeightScale"]) tc.HeightScale = terrainNode["HeightScale"].as<float>();
                    if (terrainNode["TerrainSize"]) tc.TerrainSize = terrainNode["TerrainSize"].as<float>();
                    tc.SplatmapPath = loadSafePath(terrainNode, "SplatmapPath");

                    auto loadSafeTexHandle = [&](const YAML::Node& n, const std::string& k) -> AssetHandle {
                        std::string p = loadSafePath(n, k);
                        return p.empty() ? AssetHandle{} : AssetManager::Load<Texture2D>(p);
                    };

                    for (int i = 0; i < 4; i++)
                    {
                        std::string p = "Layer" + std::to_string(i);
                        tc.LayerTextures[i] = loadSafeTexHandle(terrainNode, p + "Texture");
                        tc.LayerNormalMaps[i] = loadSafeTexHandle(terrainNode, p + "NormalMap");
                        if (terrainNode[p + "Tiling"]) tc.LayerTiling[i] = terrainNode[p + "Tiling"].as<float>();
                        if (terrainNode[p + "Metallic"]) tc.LayerMetallic[i] = terrainNode[p + "Metallic"].as<float>();
                        if (terrainNode[p + "Roughness"]) tc.LayerRoughness[i] = terrainNode[p + "Roughness"].as<float>();
                    }

                    if (terrainNode["Friction"]) tc.Friction = terrainNode["Friction"].as<float>();
                    if (terrainNode["Restitution"]) tc.Restitution = terrainNode["Restitution"].as<float>();
                    if (terrainNode["LODLevels"]) tc.LODLevels = std::clamp(terrainNode["LODLevels"].as<int>(), 1, 3);
                    if (terrainNode["LODDistance1"]) tc.LODDistance1 = terrainNode["LODDistance1"].as<float>();
                    if (terrainNode["LODDistance2"]) tc.LODDistance2 = terrainNode["LODDistance2"].as<float>();
                    if (terrainNode["GrassEnabled"]) tc.GrassEnabled = terrainNode["GrassEnabled"].as<bool>();
                    if (terrainNode["GrassDensity"]) tc.GrassDensity = terrainNode["GrassDensity"].as<float>();
                    if (terrainNode["GrassHeight"]) tc.GrassHeight = terrainNode["GrassHeight"].as<float>();
                    if (terrainNode["GrassWidth"]) tc.GrassWidth = terrainNode["GrassWidth"].as<float>();
                    if (terrainNode["GrassWindStrength"]) tc.GrassWindStrength = terrainNode["GrassWindStrength"].as<float>();
                    tc.GrassTexture = loadSafeTexHandle(terrainNode, "GrassTexture");
                    tc.MeshDirty = true;
                }

                // ParticleEmitterComponent
                auto particleEmitterComponent = entityNode["ParticleEmitterComponent"];
                if (particleEmitterComponent)
                {
                    auto& pe = deserializedEntity.AddComponent<ParticleEmitterComponent>();
                    if (particleEmitterComponent["Preset"])
                    {
                        int preset = particleEmitterComponent["Preset"].as<int>();
                        if (preset >= 0 && preset <= 4)
                            pe.CurrentPreset = static_cast<ParticleEmitterComponent::Preset>(preset);
                    }
                    if (particleEmitterComponent["EmitRate"])
                        pe.EmitRate = particleEmitterComponent["EmitRate"].as<float>();
                    if (particleEmitterComponent["BurstCount"])
                        pe.BurstCount = particleEmitterComponent["BurstCount"].as<int>();
                    if (particleEmitterComponent["MaxParticles"])
                        pe.MaxParticles = particleEmitterComponent["MaxParticles"].as<uint32_t>();
                    if (particleEmitterComponent["LifeMin"])
                        pe.LifeMin = particleEmitterComponent["LifeMin"].as<float>();
                    if (particleEmitterComponent["LifeMax"])
                        pe.LifeMax = particleEmitterComponent["LifeMax"].as<float>();
                    if (particleEmitterComponent["SpeedMin"])
                        pe.SpeedMin = particleEmitterComponent["SpeedMin"].as<float>();
                    if (particleEmitterComponent["SpeedMax"])
                        pe.SpeedMax = particleEmitterComponent["SpeedMax"].as<float>();
                    if (particleEmitterComponent["SizeStart"])
                        pe.SizeStart = particleEmitterComponent["SizeStart"].as<float>();
                    if (particleEmitterComponent["SizeEnd"])
                        pe.SizeEnd = particleEmitterComponent["SizeEnd"].as<float>();
                    if (particleEmitterComponent["EmitDirection"])
                        pe.EmitDirection = particleEmitterComponent["EmitDirection"].as<glm::vec3>();
                    if (particleEmitterComponent["EmitAngle"])
                        pe.EmitAngle = particleEmitterComponent["EmitAngle"].as<float>();
                    if (particleEmitterComponent["ColorStart"])
                        pe.ColorStart = particleEmitterComponent["ColorStart"].as<glm::vec4>();
                    if (particleEmitterComponent["ColorEnd"])
                        pe.ColorEnd = particleEmitterComponent["ColorEnd"].as<glm::vec4>();
                    if (particleEmitterComponent["Gravity"])
                        pe.Gravity = particleEmitterComponent["Gravity"].as<glm::vec3>();
                    if (particleEmitterComponent["Damping"])
                        pe.Damping = particleEmitterComponent["Damping"].as<float>();
                    if (particleEmitterComponent["BlendMode"])
                    {
                        int mode = particleEmitterComponent["BlendMode"].as<int>();
                        if (mode >= 0 && mode <= 1)
                            pe.Blend = static_cast<ParticleEmitterComponent::BlendMode>(mode);
                    }
                    if (particleEmitterComponent["SPHEnabled"])
                        pe.SPHEnabled = particleEmitterComponent["SPHEnabled"].as<bool>();
                    if (particleEmitterComponent["SPH_RestDensity"])
                        pe.SPH_RestDensity = particleEmitterComponent["SPH_RestDensity"].as<float>();
                    if (particleEmitterComponent["SPH_GasConstant"])
                        pe.SPH_GasConstant = particleEmitterComponent["SPH_GasConstant"].as<float>();
                    if (particleEmitterComponent["SPH_Viscosity"])
                        pe.SPH_Viscosity = particleEmitterComponent["SPH_Viscosity"].as<float>();
                    if (particleEmitterComponent["SPH_SmoothingRadius"])
                        pe.SPH_SmoothingRadius = particleEmitterComponent["SPH_SmoothingRadius"].as<float>();
                    if (particleEmitterComponent["SPH_ParticleMass"])
                        pe.SPH_ParticleMass = particleEmitterComponent["SPH_ParticleMass"].as<float>();
                    if (particleEmitterComponent["SPH_PCISPHEnabled"])
                        pe.SPH_PCISPHEnabled = particleEmitterComponent["SPH_PCISPHEnabled"].as<bool>();
                    if (particleEmitterComponent["SPH_PCISPHIterations"])
                    {
                        int iters = particleEmitterComponent["SPH_PCISPHIterations"].as<int>();
                        pe.SPH_PCISPHIterations = std::clamp(iters, 1, 8);
                    }
                    if (particleEmitterComponent["SPH_PCISPHDelta"])
                        pe.SPH_PCISPHDelta = particleEmitterComponent["SPH_PCISPHDelta"].as<float>();
                    if (particleEmitterComponent["SPH_SurfaceTension"])
                        pe.SPH_SurfaceTension = particleEmitterComponent["SPH_SurfaceTension"].as<float>();
                    if (particleEmitterComponent["SPH_RigidBodyCoupling"])
                        pe.SPH_RigidBodyCoupling = particleEmitterComponent["SPH_RigidBodyCoupling"].as<bool>();
                    if (particleEmitterComponent["SPH_BoundaryStiffness"])
                        pe.SPH_BoundaryStiffness = particleEmitterComponent["SPH_BoundaryStiffness"].as<float>();
                    if (particleEmitterComponent["SPH_BoundaryDamping"])
                        pe.SPH_BoundaryDamping = particleEmitterComponent["SPH_BoundaryDamping"].as<float>();
                }

                // NativeScriptComponent
                auto nativeScriptNode = entityNode["NativeScriptComponent"];
                if (nativeScriptNode)
                {
                    auto& nsc = deserializedEntity.AddComponent<NativeScriptComponent>();
                    if (nativeScriptNode["ScriptName"])
                    {
                        std::string scriptName = nativeScriptNode["ScriptName"].as<std::string>();
                        nsc.ScriptName = scriptName;
                        if (!scriptName.empty())
                            ScriptRegistry::Instance().Bind(nsc, scriptName);
                    }
                }

                // AudioSourceComponent
                auto audioSourceNode = entityNode["AudioSourceComponent"];
                if (audioSourceNode)
                {
                    auto& asc = deserializedEntity.AddComponent<AudioSourceComponent>();
                    if (audioSourceNode["AudioPath"])
                    {
                        std::string path = audioSourceNode["AudioPath"].as<std::string>();
                        if (PathUtils::IsSafeAssetPath(path))
                            asc.AudioPath = path;
                        else if (!path.empty())
                            ENGINE_CORE_WARN("拒绝不安全的音频路径: {0}", path);
                    }
                    if (audioSourceNode["Volume"])
                        asc.Volume = audioSourceNode["Volume"].as<float>();
                    if (audioSourceNode["Pitch"])
                        asc.Pitch = audioSourceNode["Pitch"].as<float>();
                    if (audioSourceNode["MinDistance"])
                        asc.MinDistance = audioSourceNode["MinDistance"].as<float>();
                    if (audioSourceNode["MaxDistance"])
                        asc.MaxDistance = audioSourceNode["MaxDistance"].as<float>();
                    if (audioSourceNode["Loop"])
                        asc.Loop = audioSourceNode["Loop"].as<bool>();
                    if (audioSourceNode["PlayOnStart"])
                        asc.PlayOnStart = audioSourceNode["PlayOnStart"].as<bool>();
                    if (audioSourceNode["Spatial"])
                        asc.Spatial = audioSourceNode["Spatial"].as<bool>();
                }

                // AudioListenerComponent
                auto audioListenerNode = entityNode["AudioListenerComponent"];
                if (audioListenerNode)
                {
                    auto& alc = deserializedEntity.AddComponent<AudioListenerComponent>();
                    if (audioListenerNode["Active"])
                        alc.Active = audioListenerNode["Active"].as<bool>();
                }

                // VideoPlayerComponent
                auto videoPlayerNode = entityNode["VideoPlayerComponent"];
                if (videoPlayerNode)
                {
                    auto& vpc = deserializedEntity.AddComponent<VideoPlayerComponent>();
                    if (videoPlayerNode["StreamURL"])
                        vpc.StreamURL = videoPlayerNode["StreamURL"].as<std::string>();
                    if (videoPlayerNode["PlayOnStart"])
                        vpc.PlayOnStart = videoPlayerNode["PlayOnStart"].as<bool>();
                    if (videoPlayerNode["Loop"])
                        vpc.Loop = videoPlayerNode["Loop"].as<bool>();
                    if (videoPlayerNode["Volume"])
                        vpc.Volume = videoPlayerNode["Volume"].as<float>();
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

        // 第二遍：根据 ParentID 重建 Children 列表
        {
            auto view = m_Scene->GetAllEntitiesWith<IDComponent, RelationshipComponent>();
            for (auto entity : view)
            {
                auto& rel = m_Scene->GetRegistry().get<RelationshipComponent>(entity);
                if (static_cast<uint64_t>(rel.ParentID) != 0)
                {
                    Entity parent = m_Scene->FindEntityByUUID(rel.ParentID);
                    if (parent && parent.HasComponent<RelationshipComponent>())
                    {
                        auto& parentRel = parent.GetComponent<RelationshipComponent>();
                        UUID childUUID = m_Scene->GetRegistry().get<IDComponent>(entity).ID;
                        parentRel.Children.push_back(childUUID);
                    }
                    else
                    {
                        // 父实体不存在，重置为根节点
                        rel.ParentID = 0;
                    }
                }
            }
        }

        return true;
    }

} // namespace Engine
