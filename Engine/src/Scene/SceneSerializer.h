#pragma once

#include "Core/Base.h"

#include <string>

namespace Engine
{

    class Scene;

    class SceneSerializer
    {
    public:
        SceneSerializer(const Ref<Scene>& scene);

        bool Serialize(const std::string& filepath);
        bool Deserialize(const std::string& filepath);

    private:
        Ref<Scene> m_Scene;
    };

} // namespace Engine
