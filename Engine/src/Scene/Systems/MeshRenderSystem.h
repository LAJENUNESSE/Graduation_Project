#pragma once

#include "Core/Base.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"

#include <entt/entt.hpp>

namespace Engine
{

    class VideoRuntimeStore;
    class SceneEntityIndex;

    class MeshRenderSystem
    {
    public:
        static void SubmitRenderPackets(entt::registry& reg, RenderQueue& queue, const Ref<Shader>& pbrShader,
                                        const Ref<Texture2D>& whiteTexture,
                                        const VideoRuntimeStore* videoStore = nullptr,
                                        const SceneEntityIndex* index = nullptr);
    };

} // namespace Engine
