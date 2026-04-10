#include "engpch.h"
#include "Renderer/SPHCommon.h"
#include "Asset/AssetManager.h"
#include "Renderer/SPHKernelMath.h"
#include "Renderer/Mesh.h"
#include "Scene/Components.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>

#include <cmath>
#include <unordered_map>

// CudaTimingHelper 实现已移至 Platform/CUDA/CudaTimingHelper.cu

namespace Engine
{

    namespace
    {
        struct MeshLocalBounds
        {
            glm::vec3 Min   = glm::vec3(0.0f);
            glm::vec3 Max   = glm::vec3(0.0f);
            bool      Valid = false;
        };

        bool LoadMeshLocalBounds(const std::string& filepath, MeshLocalBounds& outBounds)
        {
            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFile(filepath, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                                                   aiProcess_PreTransformVertices);
            if (!scene || !scene->HasMeshes())
                return false;

            glm::vec3 minPos(std::numeric_limits<float>::max());
            glm::vec3 maxPos(-std::numeric_limits<float>::max());
            bool      hasVertex = false;

            for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
            {
                aiMesh* mesh = scene->mMeshes[m];
                if (!mesh || !mesh->HasPositions())
                    continue;

                for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
                {
                    const aiVector3D& v = mesh->mVertices[i];
                    glm::vec3         p(v.x, v.y, v.z);
                    minPos    = glm::min(minPos, p);
                    maxPos    = glm::max(maxPos, p);
                    hasVertex = true;
                }
            }

            if (!hasVertex)
                return false;

            outBounds.Min   = minPos;
            outBounds.Max   = maxPos;
            outBounds.Valid = true;
            return true;
        }

        bool ResolveMeshPath(entt::registry*              registry,
                             entt::entity                 entity,
                             const MeshColliderComponent& mc,
                             std::string&                 outPath)
        {
            outPath.clear();
            if (!mc.MeshPath.empty())
            {
                outPath = mc.MeshPath;
                return true;
            }

            if (!registry->all_of<MeshRendererComponent>(entity))
                return false;

            auto& mrc = registry->get<MeshRendererComponent>(entity);
            if (mrc.Type != MeshType::Model || !mrc.MeshAsset.IsValid())
                return false;

            const Ref<Mesh> mesh = AssetManager::Get<Mesh>(mrc.MeshAsset);
            if (!mesh)
                return false;

            outPath = mesh->GetModelPath();
            return !outPath.empty();
        }
    } // namespace

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

    std::vector<GPURigidBodyData>
    CollectRigidBodies(entt::registry* registry, uint32_t maxRigidBodies, RigidBodyUploadFilter filter)
    {
        std::vector<GPURigidBodyData> bodies;
        if (!registry)
            return bodies;

        const bool requireRigidBody = (filter == RigidBodyUploadFilter::RequireRigidBodyComponent);
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
            glm::quat rot      = glm::quat(tc.Rotation);
            glm::mat4 rotMat   = glm::toMat4(rot);
            glm::vec3 absScale = glm::vec3(std::abs(tc.Scale.x), std::abs(tc.Scale.y), std::abs(tc.Scale.z));

            GPURigidBodyData body{};
            body.posAndType  = glm::vec4(tc.Translation + rot * (bc.Offset * tc.Scale), 0.0f);
            body.rotCol0     = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
            body.rotCol1     = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
            body.rotCol2     = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
            body.halfExtents = glm::vec4(bc.HalfExtents * absScale, 0.0f);
            if (registry->all_of<RigidBodyComponent>(entity))
            {
                auto& rb        = registry->get<RigidBodyComponent>(entity);
                body.linearVel  = glm::vec4(rb.LinearVelocity, 0.0f);
                body.angularVel = glm::vec4(rb.AngularVelocity, 0.0f);
            }
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
            glm::quat rot    = glm::quat(tc.Rotation);
            glm::mat4 rotMat = glm::toMat4(rot);

            GPURigidBodyData body{};
            body.posAndType  = glm::vec4(tc.Translation + rot * (sc.Offset * tc.Scale), 1.0f);
            body.rotCol0     = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
            body.rotCol1     = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
            body.rotCol2     = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
            float maxScale   = std::max({std::abs(tc.Scale.x), std::abs(tc.Scale.y), std::abs(tc.Scale.z)});
            body.halfExtents = glm::vec4(sc.Radius * maxScale, 0.0f, 0.0f, 0.0f);
            if (registry->all_of<RigidBodyComponent>(entity))
            {
                auto& rb        = registry->get<RigidBodyComponent>(entity);
                body.linearVel  = glm::vec4(rb.LinearVelocity, 0.0f);
                body.angularVel = glm::vec4(rb.AngularVelocity, 0.0f);
            }
            bodies.push_back(body);
        }

        return bodies;
    }

    uint32_t UploadRigidBodiesToBuffer(entt::registry*                 registry,
                                       const Ref<ShaderStorageBuffer>& buffer,
                                       uint32_t                        maxRigidBodies,
                                       RigidBodyUploadFilter           filter)
    {
        if (!registry || !buffer)
            return 0;

        auto bodies = CollectRigidBodies(registry, maxRigidBodies, filter);
        if (!bodies.empty())
            buffer->SetData(bodies.data(), static_cast<uint32_t>(bodies.size() * sizeof(GPURigidBodyData)));

        return static_cast<uint32_t>(bodies.size());
    }

    std::vector<GPUMeshSDFData> CollectMeshSDFBodies(entt::registry*       registry,
                                                     uint32_t              maxMeshSDFBodies,
                                                     float                 defaultBlend,
                                                     RigidBodyUploadFilter filter)
    {
        std::vector<GPUMeshSDFData> bodies;
        if (!registry)
            return bodies;

        static std::unordered_map<std::string, MeshLocalBounds> s_BoundsCache;

        const bool  requireRigidBody = (filter == RigidBodyUploadFilter::RequireRigidBodyComponent);
        const float clampedBlend     = std::clamp(defaultBlend, 0.0f, 1.0f);
        bodies.reserve(maxMeshSDFBodies);

        auto meshView = registry->view<TransformComponent, MeshColliderComponent>();
        for (auto entity : meshView)
        {
            if (bodies.size() >= maxMeshSDFBodies)
                break;
            if (requireRigidBody && !registry->all_of<RigidBodyComponent>(entity))
                continue;

            auto& tc = meshView.get<TransformComponent>(entity);
            auto& mc = meshView.get<MeshColliderComponent>(entity);

            std::string meshPath;
            if (!ResolveMeshPath(registry, entity, mc, meshPath))
                continue;

            MeshLocalBounds bounds;
            if (auto it = s_BoundsCache.find(meshPath); it != s_BoundsCache.end())
            {
                bounds = it->second;
            }
            else
            {
                if (!LoadMeshLocalBounds(meshPath, bounds))
                {
                    s_BoundsCache[meshPath] = MeshLocalBounds{};
                    continue;
                }
                s_BoundsCache[meshPath] = bounds;
            }

            if (!bounds.Valid)
                continue;

            const glm::vec3 localCenter = 0.5f * (bounds.Min + bounds.Max);
            const glm::vec3 localHalf   = 0.5f * (bounds.Max - bounds.Min);

            glm::quat rot      = glm::quat(tc.Rotation);
            glm::mat4 rotMat   = glm::toMat4(rot);
            glm::vec3 absScale = glm::vec3(std::abs(tc.Scale.x), std::abs(tc.Scale.y), std::abs(tc.Scale.z));
            glm::vec3 center   = tc.Translation + rot * (localCenter * tc.Scale);

            GPUMeshSDFData body{};
            body.posAndType  = glm::vec4(center, 0.0f);
            body.rotCol0     = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
            body.rotCol1     = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
            body.rotCol2     = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
            body.halfExtents = glm::vec4(glm::max(localHalf * absScale, glm::vec3(0.001f)), 0.0f);

            glm::vec3 linearVel(0.0f);
            if (registry->all_of<RigidBodyComponent>(entity))
                linearVel = registry->get<RigidBodyComponent>(entity).LinearVelocity;
            body.linearVelAndBlend = glm::vec4(linearVel, clampedBlend);

            bodies.push_back(body);
        }

        return bodies;
    }

    uint32_t UploadMeshSDFBodiesToBuffer(entt::registry*                 registry,
                                         const Ref<ShaderStorageBuffer>& buffer,
                                         uint32_t                        maxMeshSDFBodies,
                                         float                           defaultBlend,
                                         RigidBodyUploadFilter           filter)
    {
        if (!registry || !buffer)
            return 0;

        auto bodies = CollectMeshSDFBodies(registry, maxMeshSDFBodies, defaultBlend, filter);
        if (!bodies.empty())
            buffer->SetData(bodies.data(), static_cast<uint32_t>(bodies.size() * sizeof(GPUMeshSDFData)));

        return static_cast<uint32_t>(bodies.size());
    }

} // namespace Engine
