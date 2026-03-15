#pragma once

#include <string>

namespace YAML
{
    class Emitter;
    class Node;
} // namespace YAML

namespace Engine
{
    struct ComponentMeta;
    class Scene;

    class AutoSerializer
    {
    public:
        static void Serialize(YAML::Emitter& out, const ComponentMeta& meta, void* component);
        static void Deserialize(const YAML::Node& node, const ComponentMeta& meta, void* component);
    };

} // namespace Engine
