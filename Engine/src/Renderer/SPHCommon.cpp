#include "engpch.h"
#include "Renderer/SPHCommon.h"
#include "Renderer/SPHKernelMath.h"
#include "Scene/Components.h"

#include <cmath>

#ifdef ENGINE_ENABLE_CUDA
#include "Platform/CUDA/CudaParticlePipeline.h"
#endif

namespace Engine
{

    SPHShaderSet SPHShaderSet::Load()
    {
        SPHShaderSet set;
        set.DensityShader = Shader::Create("assets/shaders/sph_density.glsl");
        set.ForceShader   = Shader::Create("assets/shaders/sph_force.glsl");
        set.PCISPHInit    = Shader::Create("assets/shaders/sph_pcisph_init.glsl");
        set.PCISPHPredict = Shader::Create("assets/shaders/sph_pcisph_predict.glsl");
        set.PCISPHDensity = Shader::Create("assets/shaders/sph_pcisph_density.glsl");
        set.PCISPHForce   = Shader::Create("assets/shaders/sph_pcisph_force.glsl");
        set.PCISPHApply   = Shader::Create("assets/shaders/sph_pcisph_apply.glsl");
        return set;
    }

    SPHKernelParams SPHKernelParams::Compute(float smoothingRadius)
    {
        auto            coeffs = SPHKernelMath::Compute(smoothingRadius);
        SPHKernelParams p;
        p.h          = coeffs.h;
        p.h2         = coeffs.h2;
        p.h6         = coeffs.h6;
        p.h9         = coeffs.h9;
        p.poly6Coeff = coeffs.poly6Coeff;
        p.spikyCoeff = coeffs.spikyCoeff;
        return p;
    }

    uint32_t UploadRigidBodiesToBuffer(entt::registry*                 registry,
                                       const Ref<ShaderStorageBuffer>& buffer,
                                       uint32_t                        maxRigidBodies,
                                       RigidBodyUploadFilter           filter)
    {
        if (!registry || !buffer)
            return 0;

        const bool                    requireRigidBody = (filter == RigidBodyUploadFilter::RequireRigidBodyComponent);
        std::vector<GPURigidBodyData> bodies;
        bodies.reserve(maxRigidBodies);

        // 收集 box collider
        auto boxView = registry->view<TransformComponent, BoxColliderComponent>();
        for (auto entity : boxView)
        {
            if (bodies.size() >= maxRigidBodies)
                break;
            if (requireRigidBody && !registry->all_of<RigidBodyComponent>(entity))
                continue;

            auto&     tc       = boxView.get<TransformComponent>(entity);
            auto&     bc       = boxView.get<BoxColliderComponent>(entity);
            glm::mat4 rotMat   = glm::toMat4(glm::quat(tc.Rotation));
            glm::vec3 absScale = glm::vec3(std::abs(tc.Scale.x), std::abs(tc.Scale.y), std::abs(tc.Scale.z));

            GPURigidBodyData body{};
            body.posAndType  = glm::vec4(tc.Translation + bc.Offset, 0.0f); // w=0 for box
            body.rotCol0     = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
            body.rotCol1     = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
            body.rotCol2     = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
            body.halfExtents = glm::vec4(bc.HalfExtents * absScale, 0.0f);
            body.linearVel   = glm::vec4(0.0f);
            body.angularVel  = glm::vec4(0.0f);
            bodies.push_back(body);
        }

        // 收集 sphere collider
        auto sphereView = registry->view<TransformComponent, SphereColliderComponent>();
        for (auto entity : sphereView)
        {
            if (bodies.size() >= maxRigidBodies)
                break;
            if (requireRigidBody && !registry->all_of<RigidBodyComponent>(entity))
                continue;

            auto&     tc     = sphereView.get<TransformComponent>(entity);
            auto&     sc     = sphereView.get<SphereColliderComponent>(entity);
            glm::mat4 rotMat = glm::toMat4(glm::quat(tc.Rotation));

            GPURigidBodyData body{};
            body.posAndType  = glm::vec4(tc.Translation + sc.Offset, 1.0f); // w=1 for sphere
            body.rotCol0     = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
            body.rotCol1     = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
            body.rotCol2     = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
            float maxScale   = std::max({std::abs(tc.Scale.x), std::abs(tc.Scale.y), std::abs(tc.Scale.z)});
            body.halfExtents = glm::vec4(sc.Radius * maxScale, 0.0f, 0.0f, 0.0f);
            body.linearVel   = glm::vec4(0.0f);
            body.angularVel  = glm::vec4(0.0f);
            bodies.push_back(body);
        }

        if (!bodies.empty())
            buffer->SetData(bodies.data(), static_cast<uint32_t>(bodies.size() * sizeof(GPURigidBodyData)));

        return static_cast<uint32_t>(bodies.size());
    }

#ifdef ENGINE_ENABLE_CUDA
    void CudaTimingHelper::Init()
    {
        EventStart = CudaInterop::CreateCudaEvent();
        EventStop  = CudaInterop::CreateCudaEvent();
        PrevStart  = CudaInterop::CreateCudaEvent();
        PrevStop   = CudaInterop::CreateCudaEvent();
    }

    void CudaTimingHelper::SwapEvents()
    {
        std::swap(EventStart, PrevStart);
        std::swap(EventStop, PrevStop);
        HasPrevTiming = true;
    }

    void CudaTimingHelper::Destroy()
    {
        if (EventStart)
            CudaInterop::DestroyCudaEvent(EventStart);
        if (EventStop)
            CudaInterop::DestroyCudaEvent(EventStop);
        if (PrevStart)
            CudaInterop::DestroyCudaEvent(PrevStart);
        if (PrevStop)
            CudaInterop::DestroyCudaEvent(PrevStop);
        EventStart = EventStop = PrevStart = PrevStop = nullptr;
        HasPrevTiming                                 = false;
    }
#endif

} // namespace Engine
