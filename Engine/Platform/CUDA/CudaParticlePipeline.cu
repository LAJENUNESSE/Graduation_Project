// 非 SPH 粒子管线的 CUDA 内核。
// 1:1 移植自 particle_emit.glsl / particle_simulate.glsl / particle_render_args.glsl。

#include "Platform/CUDA/CudaParticlePipeline.h"
#include "Platform/CUDA/CudaParticleTypes.h"
#include "Platform/CUDA/CudaErrorHandling.h"

#include <cstdint>
#include <cuda_runtime.h>

namespace Engine
{
    namespace CudaInterop
    {

        // ======================================================================
        // 设备辅助函数
        // ======================================================================

        __device__ static uint32_t pcg_hash(uint32_t v)
        {
            uint32_t state = v * 747796405u + 2891336453u;
            uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
            return (word >> 22u) ^ word;
        }

        __device__ static float pcg_randf(uint32_t seed)
        {
            return static_cast<float>(pcg_hash(seed)) / 4294967296.0f;
        }

        // ======================================================================
        // 发射内核——blockDim = 64
        // ======================================================================

        __global__ static void EmitKernel(GPUParticle* particles, uint32_t* deadList, CounterData* counter,
                                          EmitParams p)
        {
            uint32_t gid = blockIdx.x * blockDim.x + threadIdx.x;
            if (gid >= p.emitCount)
                return;

            // 从死亡列表原子弹出（与 GLSL atomicAdd 和 0xFFFFFFFF 匹配）
            uint32_t oldDead = atomicAdd(&counter->deadCount, 0xFFFFFFFFu);
            if (oldDead == 0u || oldDead > 0x80000000u)
            {
                atomicAdd(&counter->deadCount, 1u); // 撤销
                return;
            }
            uint32_t deadSlot = oldDead - 1u;
            if (deadSlot >= p.maxParticles)
            {
                atomicAdd(&counter->deadCount, 1u); // undo
                return;
            }
            uint32_t idx = deadList[deadSlot];
            if (idx >= p.maxParticles)
                return;

            // ---- 随机数生成 ----
            uint32_t seed = pcg_hash(gid + static_cast<uint32_t>(p.time * 1000.0f) * 1099u);

            float life = p.lifeMin + (p.lifeMax - p.lifeMin) * pcg_randf(seed);
            seed = pcg_hash(seed);

            // ---- 锥体内随机方向 ----
            float cosAngle = cosf(p.emitAngle);
            float z = cosAngle + (1.0f - cosAngle) * pcg_randf(seed);
            seed = pcg_hash(seed);
            float phi = pcg_randf(seed) * 6.28318530718f;
            seed = pcg_hash(seed);
            float sinTheta = sqrtf(1.0f - z * z);
            float lx = sinTheta * cosf(phi);
            float ly = sinTheta * sinf(phi);
            float lz = z;

            // 规范化发射方向
            float dx = p.emitDirection[0], dy = p.emitDirection[1], dz = p.emitDirection[2];
            float invLen = rsqrtf(dx * dx + dy * dy + dz * dz + 1e-12f);
            dx *= invLen;
            dy *= invLen;
            dz *= invLen;

            // 从 (0,0,1) → 方向构建切线/副法线
            float ux, uy, uz;
            if (fabsf(dy) < 0.999f)
            {
                ux = 0.f;
                uy = 1.f;
                uz = 0.f;
            }
            else
            {
                ux = 1.f;
                uy = 0.f;
                uz = 0.f;
            }

            // 切线 = normalize(cross(up, dir))
            float tx = uy * dz - uz * dy;
            float ty = uz * dx - ux * dz;
            float tz = ux * dy - uy * dx;
            float tInv = rsqrtf(tx * tx + ty * ty + tz * tz + 1e-12f);
            tx *= tInv;
            ty *= tInv;
            tz *= tInv;

            // 副法线 = cross(dir, tangent)
            float bx = dy * tz - dz * ty;
            float by = dz * tx - dx * tz;
            float bz = dx * ty - dy * tx;

            float wx = tx * lx + bx * ly + dx * lz;
            float wy = ty * lx + by * ly + dy * lz;
            float wz = tz * lx + bz * ly + dz * lz;

            // 随机速度
            float speed = p.speedMin + (p.speedMax - p.speedMin) * pcg_randf(seed);
            seed = pcg_hash(seed);

            // ---- 写入粒子 ----
            GPUParticle& part = particles[idx];
            part.posAndLife = {p.emitterPos[0], p.emitterPos[1], p.emitterPos[2], life};
            part.velAndMaxLife = {wx * speed, wy * speed, wz * speed, life};
            part.startColor = {p.startColor[0], p.startColor[1], p.startColor[2], p.startColor[3]};
            part.endColor = {p.endColor[0], p.endColor[1], p.endColor[2], p.endColor[3]};
            part.params = {p.sizeStart, p.sizeEnd, 0.0f, 0.0f};
        }

        // ======================================================================
        // 模拟内核——blockDim = 256
        // ======================================================================

        __global__ static void SimulateKernel(GPUParticle* particles, uint32_t* deadList, uint32_t* aliveList,
                                              CounterData* counter, SimulateParams p)
        {
            uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
            if (idx >= p.maxParticles)
                return;

            float life = particles[idx].posAndLife.w;
            if (life <= 0.0f)
                return;

            life -= p.deltaTime;

            if (life <= 0.0f)
            {
                // 粒子刚死亡
                particles[idx].posAndLife.w = 0.0f;
                uint32_t slot = atomicAdd(&counter->deadCount, 1u);
                if (slot < p.maxParticles)
                    deadList[slot] = idx;
            }
            else
            {
                particles[idx].posAndLife.w = life;

                float vx = particles[idx].velAndMaxLife.x;
                float vy = particles[idx].velAndMaxLife.y;
                float vz = particles[idx].velAndMaxLife.z;

                vx += p.gravity[0] * p.deltaTime;
                vy += p.gravity[1] * p.deltaTime;
                vz += p.gravity[2] * p.deltaTime;
                vx *= p.damping;
                vy *= p.damping;
                vz *= p.damping;

                particles[idx].velAndMaxLife.x = vx;
                particles[idx].velAndMaxLife.y = vy;
                particles[idx].velAndMaxLife.z = vz;

                particles[idx].posAndLife.x += vx * p.deltaTime;
                particles[idx].posAndLife.y += vy * p.deltaTime;
                particles[idx].posAndLife.z += vz * p.deltaTime;

                uint32_t slot = atomicAdd(&counter->aliveCount, 1u);
                if (slot < p.maxParticles)
                    aliveList[slot] = idx;
            }
        }

        // ======================================================================
        // 渲染参数内核——1 个线程
        // ======================================================================

        __global__ static void RenderArgsKernel(CounterData* counter, IndirectDrawCommand* args)
        {
            args->instanceCount = counter->aliveCount;
        }

        // ======================================================================
        // 启动包装器（主机，从 C++ 调用）
        // ======================================================================

        void LaunchEmit(void* particles, void* deadList, void* counter, const EmitParams& params, void* stream)
        {
            if (IsCudaPoisoned() || params.emitCount == 0)
                return;

            uint32_t blocks = (params.emitCount + 63) / 64;
            EmitKernel<<<blocks, 64, 0, static_cast<cudaStream_t>(stream)>>>(
                static_cast<GPUParticle*>(particles), static_cast<uint32_t*>(deadList),
                static_cast<CounterData*>(counter), params);
            CUDA_CHECK_KERNEL("EmitKernel");
        }

        void LaunchSimulate(void* particles, void* deadList, void* aliveList, void* counter,
                            const SimulateParams& params, void* stream)
        {
            if (IsCudaPoisoned() || params.maxParticles == 0)
                return;

            uint32_t blocks = (params.maxParticles + 255) / 256;
            SimulateKernel<<<blocks, 256, 0, static_cast<cudaStream_t>(stream)>>>(
                static_cast<GPUParticle*>(particles), static_cast<uint32_t*>(deadList),
                static_cast<uint32_t*>(aliveList), static_cast<CounterData*>(counter), params);
            CUDA_CHECK_KERNEL("SimulateKernel");
        }

        void LaunchRenderArgs(void* counter, void* indirectArgs, void* stream)
        {
            if (IsCudaPoisoned()) return;

            RenderArgsKernel<<<1, 1, 0, static_cast<cudaStream_t>(stream)>>>(
                static_cast<CounterData*>(counter), static_cast<IndirectDrawCommand*>(indirectArgs));
            CUDA_CHECK_KERNEL("RenderArgsKernel");
        }

        // ======================================================================
        // CUDA event 计时辅助
        // ======================================================================

        void* CreateCudaEvent()
        {
            if (IsCudaPoisoned()) return nullptr;
            cudaEvent_t ev = nullptr;
            CUDA_CHECK(cudaEventCreate(&ev));
            return static_cast<void*>(ev);
        }

        void DestroyCudaEvent(void* event)
        {
            if (event)
                cudaEventDestroy(static_cast<cudaEvent_t>(event));
        }

        void RecordCudaEvent(void* event, void* stream)
        {
            if (IsCudaPoisoned() || !event) return;
            CUDA_CHECK(cudaEventRecord(static_cast<cudaEvent_t>(event), static_cast<cudaStream_t>(stream)));
        }

        float CudaEventElapsedMs(void* start, void* stop)
        {
            if (IsCudaPoisoned() || !start || !stop) return 0.0f;
            CUDA_CHECK(cudaEventSynchronize(static_cast<cudaEvent_t>(stop)));
            float ms = 0.0f;
            CUDA_CHECK(cudaEventElapsedTime(&ms, static_cast<cudaEvent_t>(start), static_cast<cudaEvent_t>(stop)));
            return ms;
        }

    } // namespace CudaInterop
} // namespace Engine
