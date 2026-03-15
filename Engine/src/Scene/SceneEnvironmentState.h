#pragma once

#include "Scene/Systems/ShadowSystem.h"

#include <string>
#include <vector>

namespace Engine
{

    struct SceneEnvironmentState
    {
        ShadowSettings Shadow;
        std::vector<std::string> SkyboxFacePaths;
    };

} // namespace Engine
