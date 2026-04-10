#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
#include "Renderer/StorageBuffer.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace Engine
{

    // SPH shader 集合：粒子系统和流体系统共享的 7 个 shader
    struct SPHShaderSet
    {
        Ref<Shader> DensityShader;
        Ref<Shader> ForceShader;
        Ref<Shader> PCISPHInit;
        Ref<Shader> PCISPHPredict;
        Ref<Shader> PCISPHDensity;
        Ref<Shader> PCISPHForce;
        Ref<Shader> PCISPHApply;

        static SPHShaderSet Load();
    };

    // SPH kernel 预计算常量（避免 GPU 每粒子每邻居重复计算）
    struct SPHKernelParams
    {
        float h;          // smoothing radius
        float h2;         // h^2
        float h6;         // h^6
        float h9;         // h^9
        float poly6Coeff; // 315 / (64 * pi * h^9)
        float spikyCoeff; // -45 / (pi * h^6)

        static SPHKernelParams Compute(float smoothingRadius);
    };

    // 统一的 GPU 刚体数据结构：7 x vec4 = 112 bytes
    // 与 GLSL GPURigidBody 和 CUDA RigidBodyData 布局完全一致
    struct GPURigidBodyData
    {
        glm::vec4 posAndType; // xyz=center, w=0(box)/1(sphere)
        glm::vec4 rotCol0;
        glm::vec4 rotCol1;
        glm::vec4 rotCol2;
        glm::vec4 halfExtents; // box: xyz=半尺寸; sphere: x=radius
        glm::vec4 linearVel;
        glm::vec4 angularVel;
    };

    // Mesh SDF 元数据（真实体素 SDF 采样）
    struct GPUMeshSDFData
    {
        glm::vec4 posAndType; // xyz=world translation, w=0
        glm::vec4 rotCol0;
        glm::vec4 rotCol1;
        glm::vec4 rotCol2;
        glm::vec4 invScaleAndBlend; // xyz=1/abs(scale), w=blend
        glm::vec4 localMin;         // xyz=mesh local AABB min
        glm::vec4 localExtent;      // xyz=mesh local AABB extent
        glm::vec4 gridParams;       // x=resolution, y=voxelOffset, z=voxelCount, w=band
    };

    struct MeshSDFUploadResult
    {
        uint32_t BodyCount  = 0;
        uint32_t VoxelCount = 0;
    };

    enum class RigidBodyUploadFilter
    {
        AllColliders = 0,
        RequireRigidBodyComponent
    };

    // 从 registry 收集带碰撞器的实体，上传到 GPU buffer
    // 返回实际上传的刚体数量
    static constexpr uint32_t MAX_RIGID_BODIES        = 64;
    static constexpr uint32_t MAX_MESH_SDF_BODIES     = 16;
    static constexpr uint32_t MAX_MESH_SDF_RESOLUTION = 32;
    static constexpr uint32_t MAX_MESH_SDF_VOXELS =
        MAX_MESH_SDF_BODIES * MAX_MESH_SDF_RESOLUTION * MAX_MESH_SDF_RESOLUTION * MAX_MESH_SDF_RESOLUTION;

    // 从 registry 收集刚体数据（纯 CPU 侧，供 GL 和 CUDA 路径共用）
    std::vector<GPURigidBodyData>
    CollectRigidBodies(entt::registry*       registry,
                       uint32_t              maxRigidBodies,
                       RigidBodyUploadFilter filter = RigidBodyUploadFilter::AllColliders);

    // 收集 + 上传到 GL SSBO（GL 路径专用便捷函数）
    uint32_t UploadRigidBodiesToBuffer(entt::registry*                 registry,
                                       const Ref<ShaderStorageBuffer>& buffer,
                                       uint32_t                        maxRigidBodies,
                                       RigidBodyUploadFilter           filter = RigidBodyUploadFilter::AllColliders);

    // 生成并上传真实体素 SDF（元数据 + 体素数组）
    MeshSDFUploadResult UploadMeshSDFToBuffers(entt::registry*                 registry,
                                               const Ref<ShaderStorageBuffer>& metaBuffer,
                                               const Ref<ShaderStorageBuffer>& voxelBuffer,
                                               uint32_t                        maxMeshSDFBodies,
                                               uint32_t                        resolution,
                                               float                           band,
                                               float                           defaultBlend,
                                               RigidBodyUploadFilter filter = RigidBodyUploadFilter::AllColliders);

} // namespace Engine

#ifdef ENGINE_ENABLE_CUDA
#include "Platform/CUDA/CudaTimingHelper.h"
#endif
