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
#include <limits>
#include <unordered_map>

// CudaTimingHelper 实现已移至 Platform/CUDA/CudaTimingHelper.cu

namespace Engine
{

    namespace
    {
        struct MeshDataCache
        {
            std::vector<glm::vec3> Vertices;
            std::vector<uint32_t>  Indices;
            glm::vec3              Min   = glm::vec3(0.0f);
            glm::vec3              Max   = glm::vec3(0.0f);
            bool                   Valid = false;
        };

        struct VoxelGridBuild
        {
            std::vector<float> Voxels;
            uint32_t           Resolution = 0;
            float              Band       = 0.0f;
            uint32_t           Count      = 0;
        };

        inline uint32_t Flatten3D(uint32_t x, uint32_t y, uint32_t z, uint32_t res)
        {
            return z * res * res + y * res + x;
        }

        inline glm::vec3
        ClosestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
        {
            const glm::vec3 ab = b - a;
            const glm::vec3 ac = c - a;
            const glm::vec3 ap = p - a;

            float d1 = glm::dot(ab, ap);
            float d2 = glm::dot(ac, ap);
            if (d1 <= 0.0f && d2 <= 0.0f)
                return a;

            const glm::vec3 bp = p - b;
            float           d3 = glm::dot(ab, bp);
            float           d4 = glm::dot(ac, bp);
            if (d3 >= 0.0f && d4 <= d3)
                return b;

            float vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
            {
                float v = d1 / (d1 - d3);
                return a + v * ab;
            }

            const glm::vec3 cp = p - c;
            float           d5 = glm::dot(ab, cp);
            float           d6 = glm::dot(ac, cp);
            if (d6 >= 0.0f && d5 <= d6)
                return c;

            float vb = d5 * d2 - d1 * d6;
            if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
            {
                float w = d2 / (d2 - d6);
                return a + w * ac;
            }

            float va = d3 * d6 - d5 * d4;
            if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
            {
                float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                return b + w * (c - b);
            }

            float denom = 1.0f / (va + vb + vc);
            float v     = vb * denom;
            float w     = vc * denom;
            return a + ab * v + ac * w;
        }

        inline bool RayTriangleIntersect(const glm::vec3& origin,
                                         const glm::vec3& dir,
                                         const glm::vec3& v0,
                                         const glm::vec3& v1,
                                         const glm::vec3& v2,
                                         float&           outT)
        {
            constexpr float kEpsilon = 1e-6f;
            const glm::vec3 e1       = v1 - v0;
            const glm::vec3 e2       = v2 - v0;
            const glm::vec3 pvec     = glm::cross(dir, e2);
            const float     det      = glm::dot(e1, pvec);
            if (std::abs(det) < kEpsilon)
                return false;

            const float     invDet = 1.0f / det;
            const glm::vec3 tvec   = origin - v0;
            const float     u      = glm::dot(tvec, pvec) * invDet;
            if (u < 0.0f || u > 1.0f)
                return false;

            const glm::vec3 qvec = glm::cross(tvec, e1);
            const float     v    = glm::dot(dir, qvec) * invDet;
            if (v < 0.0f || (u + v) > 1.0f)
                return false;

            const float t = glm::dot(e2, qvec) * invDet;
            if (t < kEpsilon)
                return false;

            outT = t;
            return true;
        }

        bool LoadMeshDataCache(const std::string& filepath, MeshDataCache& outData)
        {
            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFile(filepath, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                                                   aiProcess_PreTransformVertices);
            if (!scene || !scene->HasMeshes())
                return false;

            glm::vec3 minPos(std::numeric_limits<float>::max());
            glm::vec3 maxPos(-std::numeric_limits<float>::max());
            bool      hasVertex = false;

            outData.Vertices.clear();
            outData.Indices.clear();

            for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
            {
                aiMesh* mesh = scene->mMeshes[m];
                if (!mesh || !mesh->HasPositions())
                    continue;

                const uint32_t baseVertex = static_cast<uint32_t>(outData.Vertices.size());

                for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
                {
                    const aiVector3D& v = mesh->mVertices[i];
                    glm::vec3         p(v.x, v.y, v.z);
                    minPos    = glm::min(minPos, p);
                    maxPos    = glm::max(maxPos, p);
                    hasVertex = true;
                    outData.Vertices.push_back(p);
                }

                for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
                {
                    const aiFace& face = mesh->mFaces[f];
                    if (face.mNumIndices != 3)
                        continue;
                    outData.Indices.push_back(baseVertex + face.mIndices[0]);
                    outData.Indices.push_back(baseVertex + face.mIndices[1]);
                    outData.Indices.push_back(baseVertex + face.mIndices[2]);
                }
            }

            if (!hasVertex || outData.Indices.empty())
                return false;

            outData.Min   = minPos;
            outData.Max   = maxPos;
            outData.Valid = true;
            return true;
        }

        VoxelGridBuild BuildVoxelSDF(const MeshDataCache& meshData, uint32_t resolution, float band)
        {
            VoxelGridBuild out;
            if (!meshData.Valid || meshData.Indices.empty() || resolution == 0)
                return out;

            const glm::vec3 extent = glm::max(meshData.Max - meshData.Min, glm::vec3(1e-4f));
            const glm::vec3 step   = extent / static_cast<float>(resolution);
            const uint32_t  count  = resolution * resolution * resolution;

            out.Voxels.resize(count, 0.0f);
            out.Resolution = resolution;
            out.Band       = std::max(0.0f, band);
            out.Count      = count;

            for (uint32_t z = 0; z < resolution; ++z)
            {
                for (uint32_t y = 0; y < resolution; ++y)
                {
                    for (uint32_t x = 0; x < resolution; ++x)
                    {
                        const glm::vec3 p = meshData.Min + (glm::vec3(static_cast<float>(x), static_cast<float>(y),
                                                                      static_cast<float>(z)) +
                                                            glm::vec3(0.5f)) *
                                                               step;

                        float minDist2 = std::numeric_limits<float>::max();
                        for (size_t i = 0; i + 2 < meshData.Indices.size(); i += 3)
                        {
                            const glm::vec3& a  = meshData.Vertices[meshData.Indices[i + 0]];
                            const glm::vec3& b  = meshData.Vertices[meshData.Indices[i + 1]];
                            const glm::vec3& c  = meshData.Vertices[meshData.Indices[i + 2]];
                            glm::vec3        cp = ClosestPointOnTriangle(p, a, b, c);
                            float            d2 = glm::length2(p - cp);
                            minDist2            = std::min(minDist2, d2);
                        }

                        float dist = std::sqrt(std::max(minDist2, 0.0f));

                        // 沿 +X 射线奇偶判定 inside/outside
                        int             hits = 0;
                        const glm::vec3 dir(1.0f, 0.0f, 0.0f);
                        constexpr float kBias     = 1e-4f;
                        const glm::vec3 rayOrigin = p + glm::vec3(kBias, 0.0f, 0.0f);
                        for (size_t i = 0; i + 2 < meshData.Indices.size(); i += 3)
                        {
                            const glm::vec3& a = meshData.Vertices[meshData.Indices[i + 0]];
                            const glm::vec3& b = meshData.Vertices[meshData.Indices[i + 1]];
                            const glm::vec3& c = meshData.Vertices[meshData.Indices[i + 2]];
                            float            t = 0.0f;
                            if (RayTriangleIntersect(rayOrigin, dir, a, b, c, t))
                                ++hits;
                        }
                        const bool inside                          = (hits % 2) == 1;
                        out.Voxels[Flatten3D(x, y, z, resolution)] = inside ? -dist : dist;
                    }
                }
            }

            return out;
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

            Mesh* mesh = AssetManager::Get<Mesh>(mrc.MeshAsset);
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

    MeshSDFUploadResult UploadMeshSDFToBuffers(entt::registry*                 registry,
                                               const Ref<ShaderStorageBuffer>& metaBuffer,
                                               const Ref<ShaderStorageBuffer>& voxelBuffer,
                                               uint32_t                        maxMeshSDFBodies,
                                               uint32_t                        resolution,
                                               float                           band,
                                               float                           defaultBlend,
                                               RigidBodyUploadFilter           filter)
    {
        MeshSDFUploadResult result{};
        if (!registry || !metaBuffer || !voxelBuffer)
            return result;

        static std::unordered_map<std::string, MeshDataCache> s_MeshCache;

        const bool     requireRigidBody = (filter == RigidBodyUploadFilter::RequireRigidBodyComponent);
        const float    clampedBlend     = std::clamp(defaultBlend, 0.0f, 1.0f);
        const uint32_t clampedRes       = std::clamp(resolution, 8u, MAX_MESH_SDF_RESOLUTION);
        const float    clampedBand      = std::max(0.0f, band);

        std::vector<GPUMeshSDFData> metaList;
        std::vector<float>          voxelData;
        metaList.reserve(maxMeshSDFBodies);
        voxelData.reserve(MAX_MESH_SDF_VOXELS);

        auto meshView = registry->view<TransformComponent, MeshColliderComponent>();
        for (auto entity : meshView)
        {
            if (metaList.size() >= maxMeshSDFBodies)
                break;
            if (requireRigidBody && !registry->all_of<RigidBodyComponent>(entity))
                continue;

            auto& tc = meshView.get<TransformComponent>(entity);
            auto& mc = meshView.get<MeshColliderComponent>(entity);

            std::string meshPath;
            if (!ResolveMeshPath(registry, entity, mc, meshPath))
                continue;

            MeshDataCache meshData;
            if (auto it = s_MeshCache.find(meshPath); it != s_MeshCache.end())
            {
                meshData = it->second;
            }
            else
            {
                if (!LoadMeshDataCache(meshPath, meshData))
                {
                    s_MeshCache[meshPath] = MeshDataCache{};
                    continue;
                }
                s_MeshCache[meshPath] = meshData;
            }

            if (!meshData.Valid)
                continue;

            VoxelGridBuild grid = BuildVoxelSDF(meshData, clampedRes, clampedBand);
            if (grid.Count == 0 || grid.Voxels.empty())
                continue;
            if (voxelData.size() + grid.Voxels.size() > MAX_MESH_SDF_VOXELS)
                break;

            const uint32_t voxelOffset = static_cast<uint32_t>(voxelData.size());
            voxelData.insert(voxelData.end(), grid.Voxels.begin(), grid.Voxels.end());

            const glm::vec3 absScale = glm::max(glm::abs(tc.Scale), glm::vec3(1e-4f));

            glm::quat rot    = glm::quat(tc.Rotation);
            glm::mat4 rotMat = glm::toMat4(rot);

            GPUMeshSDFData body{};
            body.posAndType       = glm::vec4(tc.Translation, 0.0f);
            body.rotCol0          = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
            body.rotCol1          = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
            body.rotCol2          = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
            body.invScaleAndBlend = glm::vec4(1.0f / absScale, clampedBlend);
            body.localMin         = glm::vec4(meshData.Min, 0.0f);
            body.localExtent      = glm::vec4(glm::max(meshData.Max - meshData.Min, glm::vec3(1e-4f)), 0.0f);
            body.gridParams       = glm::vec4(static_cast<float>(grid.Resolution), static_cast<float>(voxelOffset),
                                              static_cast<float>(grid.Count), grid.Band);
            metaList.push_back(body);
        }

        if (!metaList.empty())
            metaBuffer->SetData(metaList.data(), static_cast<uint32_t>(metaList.size() * sizeof(GPUMeshSDFData)));
        if (!voxelData.empty())
            voxelBuffer->SetData(voxelData.data(), static_cast<uint32_t>(voxelData.size() * sizeof(float)));

        result.BodyCount  = static_cast<uint32_t>(metaList.size());
        result.VoxelCount = static_cast<uint32_t>(voxelData.size());
        return result;
    }

} // namespace Engine
