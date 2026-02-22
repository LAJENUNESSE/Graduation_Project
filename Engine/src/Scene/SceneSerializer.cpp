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
            out << YAML::Key << "Color" << YAML::Value << mrc.Color;
            out << YAML::Key << "TexturePath" << YAML::Value << mrc.TexturePath;
            out << YAML::Key << "Tiling" << YAML::Value << mrc.Tiling;
            out << YAML::Key << "Shininess" << YAML::Value << mrc.Shininess;
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

        out << YAML::EndMap;
    }

    bool SceneSerializer::Serialize(const std::string& filepath)
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

    static Ref<Mesh> CreateMeshFromType(const std::string& meshType)
    {
        if (meshType == "Cube")
            return Mesh::CreateCube();
        if (meshType == "Plane")
            return Mesh::CreatePlane();
        if (meshType == "Sphere")
            return Mesh::CreateSphere();
        return Mesh::CreateCube();
    }

    bool SceneSerializer::Deserialize(const std::string& filepath)
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
                    mrc.MeshData = CreateMeshFromType(meshType);
                    mrc.Color = meshRendererComponent["Color"].as<glm::vec4>();

                    if (meshRendererComponent["TexturePath"])
                    {
                        std::string texPath = meshRendererComponent["TexturePath"].as<std::string>();
                        // Reject absolute paths and directory traversal
                        if (!texPath.empty() && texPath[0] != '/' && texPath.find("..") == std::string::npos)
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

                    cc.Camera.SetProjectionType(
                        static_cast<SceneCamera::ProjectionType>(cameraComponent["ProjectionType"].as<int>()));
                    cc.Camera.SetPerspectiveVerticalFOV(cameraComponent["PerspectiveFOV"].as<float>());
                    cc.Camera.SetPerspectiveNearClip(cameraComponent["PerspectiveNear"].as<float>());
                    cc.Camera.SetPerspectiveFarClip(cameraComponent["PerspectiveFar"].as<float>());
                    cc.Camera.SetOrthographicSize(cameraComponent["OrthographicSize"].as<float>());
                    cc.Camera.SetOrthographicNearClip(cameraComponent["OrthographicNear"].as<float>());
                    cc.Camera.SetOrthographicFarClip(cameraComponent["OrthographicFar"].as<float>());
                    cc.Primary = cameraComponent["Primary"].as<bool>();
                    cc.FixedAspectRatio = cameraComponent["FixedAspectRatio"].as<bool>();
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
