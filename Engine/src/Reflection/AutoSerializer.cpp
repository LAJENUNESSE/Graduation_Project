#include "Reflection/AutoSerializer.h"
#include "Asset/PathUtils.h"
#include "Core/Log.h"
#include "Reflection/ComponentMeta.h"
#include "engpch.h"

#include <glm/glm.hpp>
#include <yaml-cpp/yaml.h>

namespace Engine
{

    void AutoSerializer::Serialize(YAML::Emitter& out, const ComponentMeta& meta, void* component)
    {
        for (auto& prop : meta.Properties)
        {
            if (prop.Hints.Transient)
                continue;

            void* ptr = static_cast<char*>(component) + prop.Offset;

            switch (prop.Type)
            {
            case PropertyType::Float:
                out << YAML::Key << prop.Name << YAML::Value << *static_cast<float*>(ptr);
                break;
            case PropertyType::Int:
                out << YAML::Key << prop.Name << YAML::Value << *static_cast<int*>(ptr);
                break;
            case PropertyType::UInt32:
                out << YAML::Key << prop.Name << YAML::Value << *static_cast<uint32_t*>(ptr);
                break;
            case PropertyType::Bool:
                out << YAML::Key << prop.Name << YAML::Value << *static_cast<bool*>(ptr);
                break;
            case PropertyType::String:
            case PropertyType::AssetPath:
                out << YAML::Key << prop.Name << YAML::Value << *static_cast<std::string*>(ptr);
                break;
            case PropertyType::Vec2:
            {
                auto& v = *static_cast<glm::vec2*>(ptr);
                out << YAML::Key << prop.Name << YAML::Value;
                out << YAML::Flow << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
                break;
            }
            case PropertyType::Vec3:
            case PropertyType::Color3:
            {
                auto& v = *static_cast<glm::vec3*>(ptr);
                out << YAML::Key << prop.Name << YAML::Value;
                out << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
                break;
            }
            case PropertyType::Vec4:
            case PropertyType::Color4:
            {
                auto& v = *static_cast<glm::vec4*>(ptr);
                out << YAML::Key << prop.Name << YAML::Value;
                out << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << v.w << YAML::EndSeq;
                break;
            }
            case PropertyType::Enum:
                out << YAML::Key << prop.Name << YAML::Value << *static_cast<int*>(ptr);
                break;
            }
        }
    }

    void AutoSerializer::Deserialize(const YAML::Node& node, const ComponentMeta& meta, void* component)
    {
        for (auto& prop : meta.Properties)
        {
            if (prop.Hints.Transient)
                continue;

            auto fieldNode = node[prop.Name];
            if (!fieldNode)
                continue;

            void* ptr = static_cast<char*>(component) + prop.Offset;

            try
            {
                switch (prop.Type)
                {
                case PropertyType::Float:
                    *static_cast<float*>(ptr) = fieldNode.as<float>();
                    break;
                case PropertyType::Int:
                    *static_cast<int*>(ptr) = fieldNode.as<int>();
                    break;
                case PropertyType::UInt32:
                    *static_cast<uint32_t*>(ptr) = fieldNode.as<uint32_t>();
                    break;
                case PropertyType::Bool:
                    *static_cast<bool*>(ptr) = fieldNode.as<bool>();
                    break;
                case PropertyType::String:
                    *static_cast<std::string*>(ptr) = fieldNode.as<std::string>();
                    break;
                case PropertyType::AssetPath:
                {
                    std::string path = fieldNode.as<std::string>();
                    if (path.empty() || PathUtils::IsSafeAssetPath(path))
                        *static_cast<std::string*>(ptr) = path;
                    else
                        ENGINE_CORE_WARN("AutoSerializer: 拒绝不安全路径 {0}", path);
                    break;
                }
                case PropertyType::Vec2:
                    if (fieldNode.IsSequence() && fieldNode.size() == 2)
                    {
                        auto& v = *static_cast<glm::vec2*>(ptr);
                        v.x = fieldNode[0].as<float>();
                        v.y = fieldNode[1].as<float>();
                    }
                    break;
                case PropertyType::Vec3:
                case PropertyType::Color3:
                    if (fieldNode.IsSequence() && fieldNode.size() == 3)
                    {
                        auto& v = *static_cast<glm::vec3*>(ptr);
                        v.x = fieldNode[0].as<float>();
                        v.y = fieldNode[1].as<float>();
                        v.z = fieldNode[2].as<float>();
                    }
                    break;
                case PropertyType::Vec4:
                case PropertyType::Color4:
                    if (fieldNode.IsSequence() && fieldNode.size() == 4)
                    {
                        auto& v = *static_cast<glm::vec4*>(ptr);
                        v.x = fieldNode[0].as<float>();
                        v.y = fieldNode[1].as<float>();
                        v.z = fieldNode[2].as<float>();
                        v.w = fieldNode[3].as<float>();
                    }
                    break;
                case PropertyType::Enum:
                {
                    int val = fieldNode.as<int>();
                    if (prop.Hints.EnumCount > 0 && (val < 0 || val >= prop.Hints.EnumCount))
                        val = 0;
                    *static_cast<int*>(ptr) = val;
                    break;
                }
                }
            }
            catch (const YAML::Exception& e)
            {
                ENGINE_CORE_WARN("AutoSerializer: 反序列化字段 {0} 失败: {1}", prop.Name, e.what());
            }
        }
    }

} // namespace Engine
