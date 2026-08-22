// SPH/PCISPH 流体管线的 CUDA 内核。
// 1:1 移植自对应 GLSL compute shader（sph_density/sph_force/sph_pcisph_*/fluid_simulate/fluid_emit）。
// Grid Build 默认使用 CUB DeviceScan::ExclusiveSum；ENGINE_CUDA_SCAN=blelloch 时
// 切换到三 pass Blelloch 扫描（与 grid_prefix_sum.glsl 算法一致），用于消融实验。

#include "Platform/CUDA/CudaParticleTypes.h"
#include "Platform/CUDA/CudaSPHPipeline.h"
#include "Platform/CUDA/CudaErrorHandling.h"

#include <cuda_runtime.h>
#include <cub/device/device_scan.cuh>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace Engine
{
    namespace CudaInterop
    {

        // ======================================================================
        // SPH Context（持有 CUDA-native 网格缓冲区）
        // ======================================================================

        struct SPHContextImpl
        {
            uint32_t*      d_cellHash;       // N × uint32
            uint32_t*      d_cellCount;      // G³ × uint32
            uint32_t*      d_cellStart;      // G³ × uint32
            uint32_t*      d_sortedIndices;  // N × uint32
            PCISPHData*    d_pcisph;         // N × 48B
            RigidBodyData* d_rigidBody;      // maxRB × 112B
            Vec4*          d_surfaceNormals; // N × Vec4 (Akinci 表面法线)
            uint32_t*      d_blockSums;      // Blelloch 辅助（≥ ceil(G³/512)，消融开关用）
            void*          d_cubTemp;        // CUB 临时存储
            size_t         cubTempBytes;
            uint32_t       maxParticles;
            uint32_t       gridSize; // 每轴格子数（gridSize³ = 总格子数）
            uint32_t       maxRigidBodies;
            bool           useBlellochScan = false; // 消融开关：true 时用三 pass Blelloch 替代 CUB（ENGINE_CUDA_SCAN）
        };

        void* CreateSPHContext(uint32_t maxParticles, uint32_t gridSize, uint32_t maxRigidBodies)
        {
            if (IsCudaPoisoned())
                return nullptr;

            SPHContextImpl* ctx = new SPHContextImpl{};
            ctx->maxParticles   = maxParticles;
            ctx->gridSize       = gridSize;
            ctx->maxRigidBodies = maxRigidBodies;

            // 消融开关：ENGINE_CUDA_SCAN=blelloch 时用三 pass Blelloch 扫描替代 CUB，
            // 用于剥离"前缀和算法实现质量"与"CUDA API 本身效率"的混淆变量。默认 cub。
            if (const char* scanEnv = std::getenv("ENGINE_CUDA_SCAN"))
                ctx->useBlellochScan = (std::strcmp(scanEnv, "blelloch") == 0);

            uint32_t totalCells = gridSize * gridSize * gridSize;

            bool ok = true;
            ok      = ok && CUDA_CHECK(cudaMalloc(&ctx->d_cellHash, maxParticles * sizeof(uint32_t)));
            ok      = ok && CUDA_CHECK(cudaMalloc(&ctx->d_cellCount, totalCells * sizeof(uint32_t)));
            ok      = ok && CUDA_CHECK(cudaMalloc(&ctx->d_cellStart, totalCells * sizeof(uint32_t)));
            ok      = ok && CUDA_CHECK(cudaMalloc(&ctx->d_sortedIndices, maxParticles * sizeof(uint32_t)));
            ok      = ok && CUDA_CHECK(cudaMalloc(&ctx->d_pcisph, maxParticles * sizeof(PCISPHData)));
            ok      = ok && CUDA_CHECK(cudaMalloc(&ctx->d_rigidBody, maxRigidBodies * sizeof(RigidBodyData)));
            ok      = ok && CUDA_CHECK(cudaMalloc(&ctx->d_surfaceNormals, maxParticles * sizeof(Vec4)));
            {
                // Blelloch 辅助缓冲：ceil(G³/512) 个块和（gridSize=64 时恰 512 个）
                uint32_t totalCellsTmp = totalCells;
                uint32_t numScanBlocks = (totalCellsTmp + 511u) / 512u;
                ok = ok && CUDA_CHECK(cudaMalloc(&ctx->d_blockSums, numScanBlocks * sizeof(uint32_t)));
            }

            if (ok)
            {
                // 调用一次 ExclusiveSum（空输入）来探测 CUB 临时内存大小
                ctx->cubTempBytes = 0;
                cub::DeviceScan::ExclusiveSum(nullptr, ctx->cubTempBytes, ctx->d_cellCount, ctx->d_cellStart,
                                              (int)totalCells);
                ok = CUDA_CHECK(cudaMalloc(&ctx->d_cubTemp, ctx->cubTempBytes));
            }

            if (!ok)
            {
                DestroySPHContext(static_cast<void*>(ctx));
                return nullptr;
            }

            return static_cast<void*>(ctx);
        }

        void DestroySPHContext(void* ctxPtr)
        {
            if (!ctxPtr)
                return;
            SPHContextImpl* ctx = static_cast<SPHContextImpl*>(ctxPtr);
            cudaFree(ctx->d_cellHash);
            cudaFree(ctx->d_cellCount);
            cudaFree(ctx->d_cellStart);
            cudaFree(ctx->d_sortedIndices);
            cudaFree(ctx->d_pcisph);
            cudaFree(ctx->d_rigidBody);
            cudaFree(ctx->d_surfaceNormals);
            cudaFree(ctx->d_blockSums);
            cudaFree(ctx->d_cubTemp);
            delete ctx;
        }

        void SPHUploadRigidBodies(void* ctxPtr, const void* cpuData, uint32_t count)
        {
            if (IsCudaPoisoned())
                return;
            SPHContextImpl* ctx = static_cast<SPHContextImpl*>(ctxPtr);
            if (!ctx || count == 0 || !cpuData)
                return;
            uint32_t uploadCount = (count < ctx->maxRigidBodies) ? count : ctx->maxRigidBodies;
            CUDA_CHECK(
                cudaMemcpy(ctx->d_rigidBody, cpuData, uploadCount * sizeof(RigidBodyData), cudaMemcpyHostToDevice));
        }

        // ======================================================================
        // 设备辅助函数
        // ======================================================================

        // Poly6 核（密度）
        __device__ static float Poly6(float r2, float h2, float coeff)
        {
            if (r2 >= h2)
                return 0.0f;
            float x = h2 - r2;
            return coeff * x * x * x;
        }

        // Spiky 梯度核（压力/粘性）
        __device__ static float SpikyGrad(float r, float h, float coeff)
        {
            if (r <= 0.0f || r >= h)
                return 0.0f;
            float x = h - r;
            return coeff * x * x;
        }

        // 网格坐标 → 线性索引（模运算环绕，与 GLSL hashCell 一致）
        __device__ static uint32_t GridIdx(int gx, int gy, int gz, int gridSize)
        {
            // 环绕到 [0, gridSize)，匹配 GLSL 的 ((cell % G) + G) % G
            gx = ((gx % gridSize) + gridSize) % gridSize;
            gy = ((gy % gridSize) + gridSize) % gridSize;
            gz = ((gz % gridSize) + gridSize) % gridSize;
            return (uint32_t)(gz * gridSize * gridSize + gy * gridSize + gx);
        }

        // 粒子位置 → 网格坐标
        __device__ static void PosToCell(float px, float py, float pz, float cellSize, int& gx, int& gy, int& gz)
        {
            gx = (int)floorf(px / cellSize);
            gy = (int)floorf(py / cellSize);
            gz = (int)floorf(pz / cellSize);
        }

        // 粘性核拉普拉斯算子: ∇²W_visc = -spikyCoeff * (h - |r|)
        __device__ static float ViscLaplacian(float r, float h, float spikyCoeff)
        {
            if (r >= h)
                return 0.0f;
            return -spikyCoeff * (h - r);
        }

        // Akinci C_spline 表面张力核
        __device__ static float CSpline(float r, float h)
        {
            constexpr float PI    = 3.14159265359f;
            float           q     = r / h;
            float           coeff = 32.0f / (PI * h * h * h);
            if (q < 0.5f)
            {
                float oq = 1.0f - q;
                return coeff * (2.0f * oq * oq * oq * q * q * q - 1.0f / 64.0f);
            }
            else if (q < 1.0f)
            {
                float oq = 1.0f - q;
                return coeff * (oq * oq * oq * q * q * q - 1.0f / 64.0f);
            }
            return 0.0f;
        }

        // 刚体 SDF 边界力（WCSPH/PCISPH 共用）
        __device__ static void ComputeRigidBodyForce(float                px,
                                                     float                py,
                                                     float                pz,
                                                     float                vx,
                                                     float                vy,
                                                     float                vz,
                                                     float                h,
                                                     float                boundaryStiffness,
                                                     float                boundaryDamping,
                                                     const RigidBodyData* d_rigidBody,
                                                     int                  rigidBodyCount,
                                                     float&               fbx,
                                                     float&               fby,
                                                     float&               fbz)
        {
            fbx = fby = fbz = 0.0f;
            for (int rb = 0; rb < rigidBodyCount; rb++)
            {
                float rbPx   = d_rigidBody[rb].posAndType.x;
                float rbPy   = d_rigidBody[rb].posAndType.y;
                float rbPz   = d_rigidBody[rb].posAndType.z;
                float rbType = d_rigidBody[rb].posAndType.w;

                // 旋转矩阵列向量
                float r00 = d_rigidBody[rb].rotCol0.x, r01 = d_rigidBody[rb].rotCol0.y, r02 = d_rigidBody[rb].rotCol0.z;
                float r10 = d_rigidBody[rb].rotCol1.x, r11 = d_rigidBody[rb].rotCol1.y, r12 = d_rigidBody[rb].rotCol1.z;
                float r20 = d_rigidBody[rb].rotCol2.x, r21 = d_rigidBody[rb].rotCol2.y, r22 = d_rigidBody[rb].rotCol2.z;

                // 世界→局部: localPos = R^T * (pos - rbPos)
                float dx = px - rbPx, dy = py - rbPy, dz = pz - rbPz;
                float lx = r00 * dx + r01 * dy + r02 * dz;
                float ly = r10 * dx + r11 * dy + r12 * dz;
                float lz = r20 * dx + r21 * dy + r22 * dz;

                float sdf;
                float lnx, lny, lnz; // 局部法线

                if (rbType < 0.5f) // Box
                {
                    float hex = d_rigidBody[rb].halfExtents.x;
                    float hey = d_rigidBody[rb].halfExtents.y;
                    float hez = d_rigidBody[rb].halfExtents.z;
                    float ddx = fabsf(lx) - hex;
                    float ddy = fabsf(ly) - hey;
                    float ddz = fabsf(lz) - hez;
                    float ox = fmaxf(ddx, 0.0f), oy = fmaxf(ddy, 0.0f), oz = fmaxf(ddz, 0.0f);
                    sdf              = sqrtf(ox * ox + oy * oy + oz * oz) + fminf(fmaxf(ddx, fmaxf(ddy, ddz)), 0.0f);
                    float sx         = (lx >= 0.0f) ? 1.0f : -1.0f;
                    float sy         = (ly >= 0.0f) ? 1.0f : -1.0f;
                    float sz         = (lz >= 0.0f) ? 1.0f : -1.0f;
                    float outsideLen = sqrtf(ox * ox + oy * oy + oz * oz);
                    if (outsideLen > 1e-6f)
                    {
                        float invOL = 1.0f / outsideLen;
                        lnx         = sx * ox * invOL;
                        lny         = sy * oy * invOL;
                        lnz         = sz * oz * invOL;
                    }
                    else if (ddx > ddy && ddx > ddz)
                    {
                        lnx = sx;
                        lny = 0.0f;
                        lnz = 0.0f;
                    }
                    else if (ddy > ddz)
                    {
                        lnx = 0.0f;
                        lny = sy;
                        lnz = 0.0f;
                    }
                    else
                    {
                        lnx = 0.0f;
                        lny = 0.0f;
                        lnz = sz;
                    }
                }
                else // Sphere
                {
                    float radius = d_rigidBody[rb].halfExtents.x;
                    float lenL   = sqrtf(lx * lx + ly * ly + lz * lz);
                    sdf          = lenL - radius;
                    if (lenL > 0.001f)
                    {
                        lnx = lx / lenL;
                        lny = ly / lenL;
                        lnz = lz / lenL;
                    }
                    else
                    {
                        lnx = 0.0f;
                        lny = 1.0f;
                        lnz = 0.0f;
                    }
                }

                if (sdf < h)
                {
                    // 局部→世界法线: worldNormal = R * localNormal
                    float wnx = r00 * lnx + r10 * lny + r20 * lnz;
                    float wny = r01 * lnx + r11 * lny + r21 * lnz;
                    float wnz = r02 * lnx + r12 * lny + r22 * lnz;

                    // 刚体表面速度: v_rb = linearVel + cross(angularVel, r)
                    float avx  = d_rigidBody[rb].angularVel.x;
                    float avy  = d_rigidBody[rb].angularVel.y;
                    float avz  = d_rigidBody[rb].angularVel.z;
                    float rbvx = d_rigidBody[rb].linearVel.x + (avy * dz - avz * dy);
                    float rbvy = d_rigidBody[rb].linearVel.y + (avz * dx - avx * dz);
                    float rbvz = d_rigidBody[rb].linearVel.z + (avx * dy - avy * dx);

                    float vrx = vx - rbvx, vry = vy - rbvy, vrz = vz - rbvz;
                    float penetration = fmaxf(0.0f, h - sdf);
                    float vnDot       = vrx * wnx + vry * wny + vrz * wnz;

                    fbx += boundaryStiffness * penetration * wnx - boundaryDamping * vnDot * wnx;
                    fby += boundaryStiffness * penetration * wny - boundaryDamping * vnDot * wny;
                    fbz += boundaryStiffness * penetration * wnz - boundaryDamping * vnDot * wnz;
                }
            }
        }

        // ======================================================================
        // Grid Build 内核（分 3 步）
        // ======================================================================

        // 步骤 1：计算每个粒子的 cell hash + 原子累计 cellCount
        // usePredictedPos=true 时从 PCISPHData 读取预测位置（PCISPH 迭代 1+）
        // d_alive 非空时 i 为存活槽索引，经其间接取池索引（与 GLSL aliveIndices 一致）；
        // d_cellHash / d_pcisph 始终按槽索引
        __global__ static void HashKernel(GPUParticle*    particles,
                                          PCISPHData*     d_pcisph,
                                          uint32_t*       d_cellHash,
                                          uint32_t*       d_cellCount,
                                          const uint32_t* d_alive,
                                          uint32_t        aliveCount,
                                          int             gridSize,
                                          float           cellSize,
                                          int             usePredictedPos)
        {
            uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= aliveCount)
                return;

            float px, py, pz;
            if (usePredictedPos && d_pcisph)
            {
                px = d_pcisph[i].predictedPosAndPressure.x;
                py = d_pcisph[i].predictedPosAndPressure.y;
                pz = d_pcisph[i].predictedPosAndPressure.z;
            }
            else
            {
                uint32_t pid = d_alive ? d_alive[i] : i;
                px           = particles[pid].posAndLife.x;
                py           = particles[pid].posAndLife.y;
                pz           = particles[pid].posAndLife.z;
            }

            int gx, gy, gz;
            PosToCell(px, py, pz, cellSize, gx, gy, gz);
            uint32_t cellIdx = GridIdx(gx, gy, gz, gridSize);

            d_cellHash[i] = cellIdx;
            atomicAdd(&d_cellCount[cellIdx], 1u);
        }

        // 步骤 3：将粒子散布到排序索引数组（CUB prefix sum 之后）
        __global__ static void
        ScatterKernel(uint32_t* d_cellHash, uint32_t* d_cellStart, uint32_t* d_sortedIndices, uint32_t aliveCount)
        {
            uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= aliveCount)
                return;

            uint32_t cellIdx      = d_cellHash[i];
            uint32_t slot         = atomicAdd(&d_cellStart[cellIdx], 1u);
            d_sortedIndices[slot] = i;
        }

        // 清零 cellCount 和 cellStart
        __global__ static void ClearGridKernel(uint32_t* d_cellCount, uint32_t* d_cellStart, uint32_t totalCells)
        {
            uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= totalCells)
                return;
            d_cellCount[i] = 0u;
            d_cellStart[i] = 0u;
        }

        // ======================================================================
        // 三 pass Blelloch exclusive scan（消融路径，1:1 移植 grid_prefix_sum.glsl）
        // blockDim=256 + shared temp[512]，算法与 OpenGL/Vulkan 路径完全一致，
        // 用于剥离"前缀和算法实现质量"变量。约束：numBlocks ≤ 512（Step 2 单
        // workgroup 容量）——gridSize=64 → G³=262144 → 512 blocks 恰好满足。
        // ======================================================================
        constexpr uint32_t kScanBlockThreads = 256;
        constexpr uint32_t kScanBlockSize    = 512; // 2 * threads

        // 注：values/outScan 不加 __restrict__——Step 2 块和扫描时两者同为 d_blockSums（in-place）
        __global__ static void ScanLocalKernel(const uint32_t* values,
                                               uint32_t*       outScan,
                                               uint32_t*       blockSums, // 可空（Step 2 复用时传 nullptr）
                                               int             n)
        {
            __shared__ uint32_t temp[kScanBlockSize];
            uint32_t            tid  = threadIdx.x;
            uint32_t            base = blockIdx.x * kScanBlockSize;
            uint32_t            ai   = tid;
            uint32_t            bi   = tid + kScanBlockThreads;
            uint32_t            gAI  = base + ai;
            uint32_t            gBI  = base + bi;

            temp[ai] = (gAI < (uint32_t)n) ? values[gAI] : 0u;
            temp[bi] = (gBI < (uint32_t)n) ? values[gBI] : 0u;

            // Up-sweep（reduce）
            uint32_t offset = 1u;
            for (uint32_t d = kScanBlockThreads; d > 0u; d >>= 1u)
            {
                __syncthreads();
                if (tid < d)
                {
                    uint32_t a = offset * (2u * tid + 1u) - 1u;
                    uint32_t b = offset * (2u * tid + 2u) - 1u;
                    temp[b] += temp[a];
                }
                offset <<= 1u;
            }

            // 保存 block 总和并清空末位
            __syncthreads();
            if (tid == 0u)
            {
                if (blockSums != nullptr)
                    blockSums[blockIdx.x] = temp[kScanBlockSize - 1u];
                temp[kScanBlockSize - 1u] = 0u;
            }

            // Down-sweep
            for (uint32_t d = 1u; d <= kScanBlockThreads; d <<= 1u)
            {
                offset >>= 1u;
                __syncthreads();
                if (tid < d)
                {
                    uint32_t a = offset * (2u * tid + 1u) - 1u;
                    uint32_t b = offset * (2u * tid + 2u) - 1u;
                    uint32_t t = temp[a];
                    temp[a]    = temp[b];
                    temp[b] += t;
                }
            }

            __syncthreads();
            if (gAI < (uint32_t)n)
                outScan[gAI] = temp[ai];
            if (gBI < (uint32_t)n)
                outScan[gBI] = temp[bi];
        }

        __global__ static void ScanPropagateKernel(uint32_t* cellStart, const uint32_t* blockSums, int n)
        {
            if (blockIdx.x == 0u)
                return;
            uint32_t blockAdd = blockSums[blockIdx.x];
            uint32_t base     = blockIdx.x * kScanBlockSize;
            uint32_t gAI      = base + threadIdx.x;
            uint32_t gBI      = base + threadIdx.x + kScanBlockThreads;
            if (gAI < (uint32_t)n)
                cellStart[gAI] += blockAdd;
            if (gBI < (uint32_t)n)
                cellStart[gBI] += blockAdd;
        }

        // Blelloch exclusive scan 入口：values → outScan（等价于 CUB ExclusiveSum）。
        // values/outScan 可为不同缓冲；blockSums 为辅助缓冲（≥ ceil(n/512) 元素）。
        static void LaunchBlellochExclusiveScan(
            uint32_t* d_values, uint32_t* d_outScan, uint32_t* d_blockSums, uint32_t totalElements, cudaStream_t strm)
        {
            const uint32_t numBlocks = (totalElements + kScanBlockSize - 1u) / kScanBlockSize;

            // Step 1: block-local scan（values → outScan，块和写入 blockSums）
            ScanLocalKernel<<<numBlocks, kScanBlockThreads, 0, strm>>>(d_values, d_outScan, d_blockSums,
                                                                       (int)totalElements);
            CUDA_CHECK_KERNEL("Blelloch::ScanLocal");

            if (numBlocks > 1u)
            {
                // Step 2: 扫描块和（in-place，单 workgroup；要求 numBlocks ≤ 512）
                ScanLocalKernel<<<1, kScanBlockThreads, 0, strm>>>(d_blockSums, d_blockSums, nullptr, (int)numBlocks);
                CUDA_CHECK_KERNEL("Blelloch::ScanBlockSums");

                // Step 3: 把块前缀加回各元素
                ScanPropagateKernel<<<numBlocks, kScanBlockThreads, 0, strm>>>(d_outScan, d_blockSums,
                                                                               (int)totalElements);
                CUDA_CHECK_KERNEL("Blelloch::Propagate");
            }
        }

        void LaunchSPHGridBuild(void*       ctxPtr,
                                void*       particles,
                                uint32_t    aliveCount,
                                int         gridSize,
                                float       cellSize,
                                void*       stream,
                                bool        usePredictedPos,
                                const void* aliveList)
        {
            if (IsCudaPoisoned())
                return;

            SPHContextImpl* ctx    = static_cast<SPHContextImpl*>(ctxPtr);
            cudaStream_t    strm   = static_cast<cudaStream_t>(stream);
            GPUParticle*    devP   = static_cast<GPUParticle*>(particles);
            const uint32_t* dAlive = static_cast<const uint32_t*>(aliveList);

            uint32_t totalCells = (uint32_t)(gridSize * gridSize * gridSize);
            uint32_t blockCells = (totalCells + 255) / 256;
            uint32_t blockParts = (aliveCount + 255) / 256;

            // 清零
            ClearGridKernel<<<blockCells, 256, 0, strm>>>(ctx->d_cellCount, ctx->d_cellStart, totalCells);
            CUDA_CHECK_KERNEL("ClearGridKernel");

            // 步骤 1：Hash（可选使用预测位置）
            HashKernel<<<blockParts, 256, 0, strm>>>(devP, ctx->d_pcisph, ctx->d_cellHash, ctx->d_cellCount, dAlive,
                                                     aliveCount, gridSize, cellSize, usePredictedPos ? 1 : 0);
            CUDA_CHECK_KERNEL("HashKernel");

            // 步骤 2：exclusive scan（cellCount → cellStart）
            // 消融开关：默认 CUB（decoupled lookback），ENGINE_CUDA_SCAN=blelloch 时
            // 用与 GL/Vulkan 完全一致的三 pass Blelloch，剥离算法实现质量变量
            if (ctx->useBlellochScan)
            {
                LaunchBlellochExclusiveScan(ctx->d_cellCount, ctx->d_cellStart, ctx->d_blockSums, totalCells, strm);
            }
            else
            {
                cub::DeviceScan::ExclusiveSum(ctx->d_cubTemp, ctx->cubTempBytes, ctx->d_cellCount, ctx->d_cellStart,
                                              (int)totalCells, strm);
                CUDA_CHECK_KERNEL("CUB::ExclusiveSum");
            }

            // 步骤 3：Scatter（将 cellStart 用作临时写偏移，atomicAdd 会破坏其原始值）
            ScatterKernel<<<blockParts, 256, 0, strm>>>(ctx->d_cellHash, ctx->d_cellStart, ctx->d_sortedIndices,
                                                        aliveCount);
            CUDA_CHECK_KERNEL("ScatterKernel");

            // 步骤 4：恢复 cellStart（scatter 的 atomicAdd 把它改坏了，重新用 scan 恢复）
            if (ctx->useBlellochScan)
            {
                LaunchBlellochExclusiveScan(ctx->d_cellCount, ctx->d_cellStart, ctx->d_blockSums, totalCells, strm);
            }
            else
            {
                cub::DeviceScan::ExclusiveSum(ctx->d_cubTemp, ctx->cubTempBytes, ctx->d_cellCount, ctx->d_cellStart,
                                              (int)totalCells, strm);
                CUDA_CHECK_KERNEL("CUB::ExclusiveSum(restore)");
            }
        }

        // ======================================================================
        // SPH Density 内核（移植自 sph_density.glsl）
        // ======================================================================

        // i 为存活槽索引（d_alive 非空时经其映射到池索引）；d_surfaceNormals 按槽索引
        __global__ static void DensityKernel(GPUParticle*    particles,
                                             uint32_t*       d_cellStart,
                                             uint32_t*       d_cellCount,
                                             uint32_t*       d_sortedIndices,
                                             const uint32_t* d_alive,
                                             Vec4*           d_surfaceNormals,
                                             SPHParams       p)
        {
            uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= (uint32_t)p.aliveCount)
                return;

            uint32_t pid = d_alive ? d_alive[i] : i;
            float    px  = particles[pid].posAndLife.x;
            float    py  = particles[pid].posAndLife.y;
            float    pz  = particles[pid].posAndLife.z;

            float h  = p.smoothingRadius;
            float h2 = h * h;

            float density = p.particleMass * Poly6(0.0f, h2, p.poly6Coeff);

            // Akinci 表面法线累加器
            float snx = 0.0f, sny = 0.0f, snz = 0.0f;

            int gx0, gy0, gz0;
            PosToCell(px, py, pz, p.cellSize, gx0, gy0, gz0);

            for (int dz = -1; dz <= 1; dz++)
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                    {
                        uint32_t cellIdx = GridIdx(gx0 + dx, gy0 + dy, gz0 + dz, p.gridSize);
                        // GridIdx 使用模运算环绕，不再需要越界检查

                        uint32_t start = d_cellStart[cellIdx];
                        uint32_t count = d_cellCount[cellIdx];
                        for (uint32_t k = 0; k < count; k++)
                        {
                            uint32_t ja = d_sortedIndices[start + k];
                            if (ja == i)
                                continue;
                            uint32_t pj  = d_alive ? d_alive[ja] : ja;
                            float    dx2 = px - particles[pj].posAndLife.x;
                            float    dy2 = py - particles[pj].posAndLife.y;
                            float    dz2 = pz - particles[pj].posAndLife.z;
                            float    r2  = dx2 * dx2 + dy2 * dy2 + dz2 * dz2;
                            density += p.particleMass * Poly6(r2, h2, p.poly6Coeff);

                            // Akinci 表面法线: n += (m / ρ_j) * ∇W_spiky
                            if (p.surfaceTension > 0.0f && r2 > 1e-12f && r2 < h2)
                            {
                                float r     = sqrtf(r2);
                                float densJ = particles[pj].params.z;
                                if (densJ < 0.0001f)
                                    densJ = 0.0001f;
                                float spikyG = SpikyGrad(r, h, p.spikyCoeff);
                                float invR   = 1.0f / r;
                                float coeff  = (p.particleMass / densJ) * spikyG * invR;
                                snx += coeff * dx2;
                                sny += coeff * dy2;
                                snz += coeff * dz2;
                            }
                        }
                    }

            particles[pid].params.z = density; // z=density
            particles[pid].params.w = fmaxf(0.0f, p.gasConstant * (density - p.restDensity));

            // 写出 Akinci 表面法线 (乘以 h)
            d_surfaceNormals[i] = Vec4{h * snx, h * sny, h * snz, 0.0f};
        }

        void LaunchSPHDensity(void* ctxPtr, void* particles, const SPHParams& p, void* stream, const void* aliveList)
        {
            if (IsCudaPoisoned())
                return;

            SPHContextImpl* ctx    = static_cast<SPHContextImpl*>(ctxPtr);
            cudaStream_t    strm   = static_cast<cudaStream_t>(stream);
            GPUParticle*    devP   = static_cast<GPUParticle*>(particles);
            const uint32_t* dAlive = static_cast<const uint32_t*>(aliveList);

            uint32_t blocks = ((uint32_t)p.aliveCount + 255) / 256;
            DensityKernel<<<blocks, 256, 0, strm>>>(devP, ctx->d_cellStart, ctx->d_cellCount, ctx->d_sortedIndices,
                                                    dAlive, ctx->d_surfaceNormals, p);
            CUDA_CHECK_KERNEL("DensityKernel");
        }

        // ======================================================================
        // SPH Force 内核（WCSPH 路径，移植自 sph_force.glsl）
        // ======================================================================

        __global__ static void ForceKernel(GPUParticle*     particles,
                                           uint32_t*        d_cellStart,
                                           uint32_t*        d_cellCount,
                                           uint32_t*        d_sortedIndices,
                                           const uint32_t*  d_alive,
                                           RigidBodyData*   d_rigidBody,
                                           const Vec4*      d_surfaceNormals,
                                           SPHParams        p,
                                           PCISPHIterParams ip)
        {
            uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= (uint32_t)p.aliveCount)
                return;

            uint32_t pid       = d_alive ? d_alive[i] : i;
            float    px        = particles[pid].posAndLife.x;
            float    py        = particles[pid].posAndLife.y;
            float    pz        = particles[pid].posAndLife.z;
            float    vx_i      = particles[pid].velAndMaxLife.x;
            float    vy_i      = particles[pid].velAndMaxLife.y;
            float    vz_i      = particles[pid].velAndMaxLife.z;
            float    densityI  = particles[pid].params.z;
            float    pressureI = particles[pid].params.w;

            float h  = p.smoothingRadius;
            float h2 = h * h;

            if (densityI < 0.0001f)
                densityI = 0.0001f;

            // 力累加（不是加速度）
            float fpx = 0.0f, fpy = 0.0f, fpz = 0.0f; // 压力
            float fvx = 0.0f, fvy = 0.0f, fvz = 0.0f; // 粘性
            float fsx = 0.0f, fsy = 0.0f, fsz = 0.0f; // 表面张力

            // Akinci 表面法线（从 DensityKernel 预计算）
            Vec4 normalI = d_surfaceNormals[i];

            int gx0, gy0, gz0;
            PosToCell(px, py, pz, p.cellSize, gx0, gy0, gz0);

            for (int dz = -1; dz <= 1; dz++)
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx2 = -1; dx2 <= 1; dx2++)
                    {
                        uint32_t cellIdx = GridIdx(gx0 + dx2, gy0 + dy, gz0 + dz, p.gridSize);
                        // GridIdx 使用模运算环绕，不再需要越界检查

                        uint32_t start = d_cellStart[cellIdx];
                        uint32_t count = d_cellCount[cellIdx];
                        for (uint32_t k = 0; k < count; k++)
                        {
                            uint32_t ja = d_sortedIndices[start + k];
                            if (ja == i)
                                continue;
                            uint32_t pj = d_alive ? d_alive[ja] : ja;

                            float rx = px - particles[pj].posAndLife.x;
                            float ry = py - particles[pj].posAndLife.y;
                            float rz = pz - particles[pj].posAndLife.z;
                            float r2 = rx * rx + ry * ry + rz * rz;
                            if (r2 >= h2 || r2 < 1e-12f)
                                continue;
                            float r = sqrtf(r2);

                            float densityJ = particles[pj].params.z;
                            if (densityJ < 0.0001f)
                                densityJ = 0.0001f;
                            float pressureJ = particles[pj].params.w;

                            // 压力力: f_press = -m * (P_i + P_j) / (2 * ρ_j) * ∇W_spiky
                            float spikyG    = SpikyGrad(r, h, p.spikyCoeff);
                            float invR      = 1.0f / r;
                            float pressTerm = -p.particleMass * (pressureI + pressureJ) / (2.0f * densityJ);
                            fpx += pressTerm * spikyG * rx * invR;
                            fpy += pressTerm * spikyG * ry * invR;
                            fpz += pressTerm * spikyG * rz * invR;

                            // 粘性力: f_visc = μ * m * (v_j - v_i) / ρ_j * ∇²W_visc
                            float viscLap = ViscLaplacian(r, h, p.spikyCoeff);
                            float viscMul = p.viscosity * p.particleMass / densityJ * viscLap;
                            fvx += viscMul * (particles[pj].velAndMaxLife.x - vx_i);
                            fvy += viscMul * (particles[pj].velAndMaxLife.y - vy_i);
                            fvz += viscMul * (particles[pj].velAndMaxLife.z - vz_i);

                            // Akinci 2013 表面张力: cohesion + curvature
                            if (p.surfaceTension > 0.0f && r > 0.001f)
                            {
                                // 内聚力
                                float cspline = CSpline(r, h);
                                float stMul   = -p.surfaceTension * p.particleMass * cspline;
                                fsx += stMul * rx * invR;
                                fsy += stMul * ry * invR;
                                fsz += stMul * rz * invR;
                                // 曲率修正: f_curvature = -γ * m_j * (n_i - n_j)
                                Vec4  normalJ = d_surfaceNormals[ja];
                                float curvMul = -p.surfaceTension * p.particleMass;
                                fsx += curvMul * (normalI.x - normalJ.x);
                                fsy += curvMul * (normalI.y - normalJ.y);
                                fsz += curvMul * (normalI.z - normalJ.z);
                            }
                        }
                    }

            // 刚体 SDF 边界力
            float fbx, fby, fbz;
            ComputeRigidBodyForce(px, py, pz, vx_i, vy_i, vz_i, h, ip.boundaryStiffness, ip.boundaryDamping,
                                  d_rigidBody, ip.rigidBodyCount, fbx, fby, fbz);

            // SPH warmup
            float life    = particles[pid].posAndLife.w;
            float maxLife = particles[pid].velAndMaxLife.w;
            float age     = maxLife - life;
            float warmup  = (p.warmupTime > 0.0f) ? fminf(fmaxf(age / p.warmupTime, 0.0f), 1.0f) : 1.0f;

            // 加速度 = (fPressure*warmup + fViscosity + fSurfaceTension)/ρ_i + fBoundary
            // 边界碰撞力作为直接加速度不除密度——保证碰撞力足以抵抗高速穿透，
            // 与 sph_force.glsl 的 SPH 加速度组装方式一致
            float ax = (fpx * warmup + fvx + fsx) / densityI + fbx;
            float ay = (fpy * warmup + fvy + fsy) / densityI + fby;
            float az = (fpz * warmup + fvz + fsz) / densityI + fbz;

            // 安全限幅
            float accelMag = sqrtf(ax * ax + ay * ay + az * az);
            if (accelMag > 500.0f)
            {
                float s = 500.0f / accelMag;
                ax *= s;
                ay *= s;
                az *= s;
            }

            // 积分速度（注意：不加重力，重力在 Simulate 阶段叠加）
            particles[pid].velAndMaxLife.x += ax * p.deltaTime;
            particles[pid].velAndMaxLife.y += ay * p.deltaTime;
            particles[pid].velAndMaxLife.z += az * p.deltaTime;
        }

        void LaunchSPHForce(void*                   ctxPtr,
                            void*                   particles,
                            const SPHParams&        p,
                            const PCISPHIterParams& ip,
                            void*                   stream,
                            const void*             aliveList)
        {
            if (IsCudaPoisoned())
                return;

            SPHContextImpl* ctx    = static_cast<SPHContextImpl*>(ctxPtr);
            cudaStream_t    strm   = static_cast<cudaStream_t>(stream);
            GPUParticle*    devP   = static_cast<GPUParticle*>(particles);
            const uint32_t* dAlive = static_cast<const uint32_t*>(aliveList);

            uint32_t blocks = ((uint32_t)p.aliveCount + 255) / 256;
            ForceKernel<<<blocks, 256, 0, strm>>>(devP, ctx->d_cellStart, ctx->d_cellCount, ctx->d_sortedIndices,
                                                  dAlive, ctx->d_rigidBody, ctx->d_surfaceNormals, p, ip);
            CUDA_CHECK_KERNEL("ForceKernel");
        }

        // ======================================================================
        // PCISPH Init 内核（移植自 sph_pcisph_init.glsl）
        // ======================================================================

        __global__ static void PCISPHInitKernel(GPUParticle*    particles,
                                                PCISPHData*     pcisph,
                                                uint32_t*       d_cellStart,
                                                uint32_t*       d_cellCount,
                                                uint32_t*       d_sortedIndices,
                                                const uint32_t* d_alive,
                                                const Vec4*     d_surfaceNormals,
                                                SPHParams       p)
        {
            uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= (uint32_t)p.aliveCount)
                return;

            // 计算非压力加速度（粘性 + 表面张力 + 重力）
            uint32_t pid      = d_alive ? d_alive[i] : i;
            float    px       = particles[pid].posAndLife.x;
            float    py       = particles[pid].posAndLife.y;
            float    pz       = particles[pid].posAndLife.z;
            float    vx_i     = particles[pid].velAndMaxLife.x;
            float    vy_i     = particles[pid].velAndMaxLife.y;
            float    vz_i     = particles[pid].velAndMaxLife.z;
            float    densityI = particles[pid].params.z;
            float    h        = p.smoothingRadius;
            float    h2       = h * h;

            if (densityI < 0.0001f)
                densityI = 0.0001f;

            // 力累加（不是加速度）
            float fvx = 0.0f, fvy = 0.0f, fvz = 0.0f; // 粘性
            float fsx = 0.0f, fsy = 0.0f, fsz = 0.0f; // 表面张力

            // Akinci 表面法线（从 DensityKernel 预计算）
            Vec4 normalI = d_surfaceNormals[i];

            int gx0, gy0, gz0;
            PosToCell(px, py, pz, p.cellSize, gx0, gy0, gz0);

            for (int dz = -1; dz <= 1; dz++)
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx2 = -1; dx2 <= 1; dx2++)
                    {
                        uint32_t cellIdx = GridIdx(gx0 + dx2, gy0 + dy, gz0 + dz, p.gridSize);
                        // GridIdx 使用模运算环绕，不再需要越界检查

                        uint32_t start = d_cellStart[cellIdx];
                        uint32_t count = d_cellCount[cellIdx];
                        for (uint32_t k = 0; k < count; k++)
                        {
                            uint32_t ja = d_sortedIndices[start + k];
                            if (ja == i)
                                continue;
                            uint32_t pj = d_alive ? d_alive[ja] : ja;

                            float rx = px - particles[pj].posAndLife.x;
                            float ry = py - particles[pj].posAndLife.y;
                            float rz = pz - particles[pj].posAndLife.z;
                            float r2 = rx * rx + ry * ry + rz * rz;
                            if (r2 >= h2 || r2 < 1e-12f)
                                continue;
                            float r = sqrtf(r2);

                            float densityJ = particles[pj].params.z;
                            if (densityJ < 0.0001f)
                                densityJ = 0.0001f;

                            // 粘性力: f_visc = μ * m * (v_j - v_i) / ρ_j * ∇²W_visc
                            float viscLap = ViscLaplacian(r, h, p.spikyCoeff);
                            float viscMul = p.viscosity * p.particleMass / densityJ * viscLap;
                            fvx += viscMul * (particles[pj].velAndMaxLife.x - vx_i);
                            fvy += viscMul * (particles[pj].velAndMaxLife.y - vy_i);
                            fvz += viscMul * (particles[pj].velAndMaxLife.z - vz_i);

                            // Akinci 2013 表面张力: cohesion + curvature
                            if (p.surfaceTension > 0.0f && r > 0.001f)
                            {
                                // 内聚力
                                float cspline = CSpline(r, h);
                                float invR    = 1.0f / r;
                                float stMul   = -p.surfaceTension * p.particleMass * cspline;
                                fsx += stMul * rx * invR;
                                fsy += stMul * ry * invR;
                                fsz += stMul * rz * invR;
                                // 曲率修正: f_curvature = -γ * m_j * (n_i - n_j)
                                Vec4  normalJ = d_surfaceNormals[ja];
                                float curvMul = -p.surfaceTension * p.particleMass;
                                fsx += curvMul * (normalI.x - normalJ.x);
                                fsy += curvMul * (normalI.y - normalJ.y);
                                fsz += curvMul * (normalI.z - normalJ.z);
                            }
                        }
                    }

            // 非压力加速度: a_np = (fViscosity + fSurfTension) / ρ_i + gravity
            float ax = (fvx + fsx) / densityI + p.gravity[0];
            float ay = (fvy + fsy) / densityI + p.gravity[1];
            float az = (fvz + fsz) / densityI + p.gravity[2];

            // 安全限幅
            float accelMag = sqrtf(ax * ax + ay * ay + az * az);
            if (accelMag > 500.0f)
            {
                float s = 500.0f / accelMag;
                ax *= s;
                ay *= s;
                az *= s;
            }

            pcisph[i].nonPressureAccel.x = ax;
            pcisph[i].nonPressureAccel.y = ay;
            pcisph[i].nonPressureAccel.z = az;
            pcisph[i].nonPressureAccel.w = 0.0f;

            // 初始化预测速度 = 当前速度 + a_np * dt
            pcisph[i].predictedVelAndDensity.x = vx_i + ax * p.deltaTime;
            pcisph[i].predictedVelAndDensity.y = vy_i + ay * p.deltaTime;
            pcisph[i].predictedVelAndDensity.z = vz_i + az * p.deltaTime;
            pcisph[i].predictedVelAndDensity.w = 0.0f;

            // 压力清零，预测位置 = 当前位置
            pcisph[i].predictedPosAndPressure.x = px;
            pcisph[i].predictedPosAndPressure.y = py;
            pcisph[i].predictedPosAndPressure.z = pz;
            pcisph[i].predictedPosAndPressure.w = 0.0f;
        }

        void LaunchPCISPHInit(void* ctxPtr, void* particles, const SPHParams& p, void* stream, const void* aliveList)
        {
            if (IsCudaPoisoned())
                return;

            SPHContextImpl* ctx    = static_cast<SPHContextImpl*>(ctxPtr);
            cudaStream_t    strm   = static_cast<cudaStream_t>(stream);
            GPUParticle*    devP   = static_cast<GPUParticle*>(particles);
            const uint32_t* dAlive = static_cast<const uint32_t*>(aliveList);

            uint32_t blocks = ((uint32_t)p.aliveCount + 255) / 256;
            PCISPHInitKernel<<<blocks, 256, 0, strm>>>(devP, ctx->d_pcisph, ctx->d_cellStart, ctx->d_cellCount,
                                                       ctx->d_sortedIndices, dAlive, ctx->d_surfaceNormals, p);
            CUDA_CHECK_KERNEL("PCISPHInitKernel");
        }

        // ======================================================================
        // PCISPH Predict 内核（移植自 sph_pcisph_predict.glsl）
        // ======================================================================

        __global__ static void PCISPHPredictKernel(
            GPUParticle* particles, PCISPHData* pcisph, const uint32_t* d_alive, float dt, uint32_t aliveCount)
        {
            uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= aliveCount)
                return;

            uint32_t pid = d_alive ? d_alive[i] : i;
            // x* = pos + dt * v*
            pcisph[i].predictedPosAndPressure.x = particles[pid].posAndLife.x + dt * pcisph[i].predictedVelAndDensity.x;
            pcisph[i].predictedPosAndPressure.y = particles[pid].posAndLife.y + dt * pcisph[i].predictedVelAndDensity.y;
            pcisph[i].predictedPosAndPressure.z = particles[pid].posAndLife.z + dt * pcisph[i].predictedVelAndDensity.z;
        }

        void LaunchPCISPHPredict(
            void* ctxPtr, void* particles, float dt, int aliveCount, void* stream, const void* aliveList)
        {
            if (IsCudaPoisoned())
                return;

            SPHContextImpl* ctx    = static_cast<SPHContextImpl*>(ctxPtr);
            cudaStream_t    strm   = static_cast<cudaStream_t>(stream);
            GPUParticle*    devP   = static_cast<GPUParticle*>(particles);
            const uint32_t* dAlive = static_cast<const uint32_t*>(aliveList);

            uint32_t blocks = ((uint32_t)aliveCount + 255) / 256;
            PCISPHPredictKernel<<<blocks, 256, 0, strm>>>(devP, ctx->d_pcisph, dAlive, dt, (uint32_t)aliveCount);
            CUDA_CHECK_KERNEL("PCISPHPredictKernel");
        }

        // ======================================================================
        // PCISPH Density 内核（移植自 sph_pcisph_density.glsl）
        // ======================================================================

        __global__ static void PCISPHDensityKernel(GPUParticle*    particles,
                                                   PCISPHData*     pcisph,
                                                   uint32_t*       d_cellStart,
                                                   uint32_t*       d_cellCount,
                                                   uint32_t*       d_sortedIndices,
                                                   const uint32_t* d_alive,
                                                   SPHParams       p,
                                                   float           pcisphDelta)
        {
            uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= (uint32_t)p.aliveCount)
                return;

            // 我的预测位置（用于距离计算）——pcisph 按槽索引，与 GLSL 一致
            float predPx = pcisph[i].predictedPosAndPressure.x;
            float predPy = pcisph[i].predictedPosAndPressure.y;
            float predPz = pcisph[i].predictedPosAndPressure.z;

            float h  = p.smoothingRadius;
            float h2 = h * h;

            // 自身贡献
            float density = p.particleMass * Poly6(0.0f, h2, p.poly6Coeff);

            // 用原始位置进行 cell 查找（grid 基于原始位置构建）
            uint32_t pid    = d_alive ? d_alive[i] : i;
            float    origPx = particles[pid].posAndLife.x;
            float    origPy = particles[pid].posAndLife.y;
            float    origPz = particles[pid].posAndLife.z;

            int gx0, gy0, gz0;
            PosToCell(origPx, origPy, origPz, p.cellSize, gx0, gy0, gz0);

            for (int dz = -1; dz <= 1; dz++)
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx2 = -1; dx2 <= 1; dx2++)
                    {
                        uint32_t cellIdx = GridIdx(gx0 + dx2, gy0 + dy, gz0 + dz, p.gridSize);
                        // GridIdx 使用模运算环绕，不再需要越界检查

                        uint32_t start = d_cellStart[cellIdx];
                        uint32_t count = d_cellCount[cellIdx];
                        for (uint32_t k = 0; k < count; k++)
                        {
                            uint32_t ja = d_sortedIndices[start + k];
                            if (ja == i)
                                continue;

                            // 用邻居的预测位置计算距离（pcisph 按槽索引）
                            float rx = predPx - pcisph[ja].predictedPosAndPressure.x;
                            float ry = predPy - pcisph[ja].predictedPosAndPressure.y;
                            float rz = predPz - pcisph[ja].predictedPosAndPressure.z;
                            float r2 = rx * rx + ry * ry + rz * rz;
                            density += p.particleMass * Poly6(r2, h2, p.poly6Coeff);
                        }
                    }

            // 输出预测密度
            pcisph[i].predictedVelAndDensity.w = density;

            // 累加压力: P += δ * max(0, ρ* - ρ₀)
            float densityErr = fmaxf(0.0f, density - p.restDensity);
            pcisph[i].predictedPosAndPressure.w += pcisphDelta * densityErr;
            pcisph[i].predictedPosAndPressure.w = fminf(pcisph[i].predictedPosAndPressure.w, 50000.0f);
        }

        void LaunchPCISPHDensity(void*                   ctxPtr,
                                 void*                   particles,
                                 const SPHParams&        p,
                                 const PCISPHIterParams& ip,
                                 void*                   stream,
                                 const void*             aliveList)
        {
            if (IsCudaPoisoned())
                return;

            SPHContextImpl* ctx    = static_cast<SPHContextImpl*>(ctxPtr);
            cudaStream_t    strm   = static_cast<cudaStream_t>(stream);
            GPUParticle*    devP   = static_cast<GPUParticle*>(particles);
            const uint32_t* dAlive = static_cast<const uint32_t*>(aliveList);

            uint32_t blocks = ((uint32_t)p.aliveCount + 255) / 256;
            PCISPHDensityKernel<<<blocks, 256, 0, strm>>>(devP, ctx->d_pcisph, ctx->d_cellStart, ctx->d_cellCount,
                                                          ctx->d_sortedIndices, dAlive, p, ip.pcisphDelta);
            CUDA_CHECK_KERNEL("PCISPHDensityKernel");
        }

        // ======================================================================
        // PCISPH Force 内核（移植自 sph_pcisph_force.glsl）
        // ======================================================================

        __global__ static void PCISPHForceKernel(GPUParticle*     particles,
                                                 PCISPHData*      pcisph,
                                                 uint32_t*        d_cellStart,
                                                 uint32_t*        d_cellCount,
                                                 uint32_t*        d_sortedIndices,
                                                 const uint32_t*  d_alive,
                                                 RigidBodyData*   d_rigidBody,
                                                 SPHParams        p,
                                                 PCISPHIterParams ip)
        {
            uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= (uint32_t)p.aliveCount)
                return;

            uint32_t pid = d_alive ? d_alive[i] : i;
            // 用原始位置进行 cell 查找（grid 基于原始位置构建）
            // 距离计算：迭代 1+ 使用预测位置（与 GLSL u_UsePredictedPos 一致）
            float px, py, pz;
            if (ip.usePredictedPos)
            {
                px = pcisph[i].predictedPosAndPressure.x;
                py = pcisph[i].predictedPosAndPressure.y;
                pz = pcisph[i].predictedPosAndPressure.z;
            }
            else
            {
                px = particles[pid].posAndLife.x;
                py = particles[pid].posAndLife.y;
                pz = particles[pid].posAndLife.z;
            }
            float pressureI = pcisph[i].predictedPosAndPressure.w;
            float densityI  = pcisph[i].predictedVelAndDensity.w;
            float h         = p.smoothingRadius;
            float h2        = h * h;

            if (densityI < 0.0001f)
                densityI = 0.0001f;

            float fpx = 0.0f, fpy = 0.0f, fpz = 0.0f; // 压力力

            int gx0, gy0, gz0;
            // grid cell 查找仍用原始位置（grid 基于原始位置构建）
            float origPx = particles[pid].posAndLife.x;
            float origPy = particles[pid].posAndLife.y;
            float origPz = particles[pid].posAndLife.z;
            PosToCell(origPx, origPy, origPz, p.cellSize, gx0, gy0, gz0);

            for (int dz = -1; dz <= 1; dz++)
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx2 = -1; dx2 <= 1; dx2++)
                    {
                        uint32_t cellIdx = GridIdx(gx0 + dx2, gy0 + dy, gz0 + dz, p.gridSize);
                        // GridIdx 使用模运算环绕，不再需要越界检查

                        uint32_t start = d_cellStart[cellIdx];
                        uint32_t count = d_cellCount[cellIdx];
                        for (uint32_t k = 0; k < count; k++)
                        {
                            uint32_t ja = d_sortedIndices[start + k];
                            if (ja == i)
                                continue;

                            // 用预测位置或原始位置计算距离（与 PCISPHDensityKernel 策略一致）
                            // pcisph 按槽索引，particles 经 alive list 间接取池索引
                            float njx = ip.usePredictedPos ? pcisph[ja].predictedPosAndPressure.x
                                                           : particles[d_alive ? d_alive[ja] : ja].posAndLife.x;
                            float njy = ip.usePredictedPos ? pcisph[ja].predictedPosAndPressure.y
                                                           : particles[d_alive ? d_alive[ja] : ja].posAndLife.y;
                            float njz = ip.usePredictedPos ? pcisph[ja].predictedPosAndPressure.z
                                                           : particles[d_alive ? d_alive[ja] : ja].posAndLife.z;
                            float rx  = px - njx;
                            float ry  = py - njy;
                            float rz  = pz - njz;
                            float r2  = rx * rx + ry * ry + rz * rz;
                            if (r2 >= h2 || r2 < 1e-12f)
                                continue;
                            float r = sqrtf(r2);

                            float pressureJ = pcisph[ja].predictedPosAndPressure.w;
                            float densityJ  = pcisph[ja].predictedVelAndDensity.w;
                            if (densityJ < 0.0001f)
                                densityJ = 0.0001f;

                            // 压力加速度 (Solenthaler 2009): -m * (P_i/ρ_i² + P_j/ρ_j²) * ∇W_spiky
                            float spikyG    = SpikyGrad(r, h, p.spikyCoeff);
                            float invR      = 1.0f / r;
                            float pressTerm = -p.particleMass *
                                              (pressureI / (densityI * densityI) + pressureJ / (densityJ * densityJ));
                            fpx += pressTerm * spikyG * rx * invR;
                            fpy += pressTerm * spikyG * ry * invR;
                            fpz += pressTerm * spikyG * rz * invR;
                        }
                    }

            // 刚体 SDF 边界力
            float fbx, fby, fbz;
            float predVx = pcisph[i].predictedVelAndDensity.x;
            float predVy = pcisph[i].predictedVelAndDensity.y;
            float predVz = pcisph[i].predictedVelAndDensity.z;
            ComputeRigidBodyForce(px, py, pz, predVx, predVy, predVz, h, ip.boundaryStiffness, ip.boundaryDamping,
                                  d_rigidBody, ip.rigidBodyCount, fbx, fby, fbz);

            // SPH warmup
            float life    = particles[pid].posAndLife.w;
            float maxLife = particles[pid].velAndMaxLife.w;
            float age     = maxLife - life;
            float warmup  = (p.warmupTime > 0.0f) ? fminf(fmaxf(age / p.warmupTime, 0.0f), 1.0f) : 1.0f;

            // 压力加速度（fPressure 已是加速度量级，fBoundary 仍需除密度）
            float ax = fpx * warmup + fbx / densityI;
            float ay = fpy * warmup + fby / densityI;
            float az = fpz * warmup + fbz / densityI;

            // 安全限幅
            float accelMag = sqrtf(ax * ax + ay * ay + az * az);
            if (accelMag > 500.0f)
            {
                float s = 500.0f / accelMag;
                ax *= s;
                ay *= s;
                az *= s;
            }

            // 更新预测速度: v* += a_pressure * dt（不重复加 a_np）
            pcisph[i].predictedVelAndDensity.x += ax * p.deltaTime;
            pcisph[i].predictedVelAndDensity.y += ay * p.deltaTime;
            pcisph[i].predictedVelAndDensity.z += az * p.deltaTime;
        }

        void LaunchPCISPHForce(void*                   ctxPtr,
                               void*                   particles,
                               const SPHParams&        p,
                               const PCISPHIterParams& ip,
                               void*                   stream,
                               const void*             aliveList)
        {
            if (IsCudaPoisoned())
                return;

            SPHContextImpl* ctx    = static_cast<SPHContextImpl*>(ctxPtr);
            cudaStream_t    strm   = static_cast<cudaStream_t>(stream);
            GPUParticle*    devP   = static_cast<GPUParticle*>(particles);
            const uint32_t* dAlive = static_cast<const uint32_t*>(aliveList);

            uint32_t blocks = ((uint32_t)p.aliveCount + 255) / 256;
            PCISPHForceKernel<<<blocks, 256, 0, strm>>>(devP, ctx->d_pcisph, ctx->d_cellStart, ctx->d_cellCount,
                                                        ctx->d_sortedIndices, dAlive, ctx->d_rigidBody, p, ip);
            CUDA_CHECK_KERNEL("PCISPHForceKernel");
        }

        // ======================================================================
        // PCISPH Apply 内核（移植自 sph_pcisph_apply.glsl）
        // ======================================================================

        __global__ static void PCISPHApplyKernel(GPUParticle*    particles,
                                                 PCISPHData*     pcisph,
                                                 const uint32_t* d_alive,
                                                 uint32_t        aliveCount,
                                                 bool            writePosition)
        {
            uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= aliveCount)
                return;

            uint32_t pid                   = d_alive ? d_alive[i] : i;
            particles[pid].velAndMaxLife.x = pcisph[i].predictedVelAndDensity.x;
            particles[pid].velAndMaxLife.y = pcisph[i].predictedVelAndDensity.y;
            particles[pid].velAndMaxLife.z = pcisph[i].predictedVelAndDensity.z;
            if (writePosition)
            {
                particles[pid].posAndLife.x = pcisph[i].predictedPosAndPressure.x;
                particles[pid].posAndLife.y = pcisph[i].predictedPosAndPressure.y;
                particles[pid].posAndLife.z = pcisph[i].predictedPosAndPressure.z;
            }
        }

        void LaunchPCISPHApply(
            void* ctxPtr, void* particles, int aliveCount, bool writePosition, void* stream, const void* aliveList)
        {
            if (IsCudaPoisoned())
                return;

            SPHContextImpl* ctx    = static_cast<SPHContextImpl*>(ctxPtr);
            cudaStream_t    strm   = static_cast<cudaStream_t>(stream);
            GPUParticle*    devP   = static_cast<GPUParticle*>(particles);
            const uint32_t* dAlive = static_cast<const uint32_t*>(aliveList);

            uint32_t blocks = ((uint32_t)aliveCount + 255) / 256;
            PCISPHApplyKernel<<<blocks, 256, 0, strm>>>(devP, ctx->d_pcisph, dAlive, (uint32_t)aliveCount,
                                                        writePosition);
            CUDA_CHECK_KERNEL("PCISPHApplyKernel");
        }

        // ======================================================================
        // Simulate 内核（移植自 fluid_simulate.glsl）
        // ======================================================================

        __global__ static void FluidSimulateKernel(GPUParticle* particles, SPHSimulateParams p)
        {
            uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= (uint32_t)p.particleCount)
                return;

            float vx = particles[i].velAndMaxLife.x;
            float vy = particles[i].velAndMaxLife.y;
            float vz = particles[i].velAndMaxLife.z;

            // PCISPH 已在 init pass 中叠加重力，避免重复积分。
            if (!p.pcisphMode)
            {
                vx += p.gravity[0] * p.deltaTime;
                vy += p.gravity[1] * p.deltaTime;
                vz += p.gravity[2] * p.deltaTime;
            }

            // 阻尼
            vx *= p.damping;
            vy *= p.damping;
            vz *= p.damping;

            float px = particles[i].posAndLife.x;
            float py = particles[i].posAndLife.y;
            float pz = particles[i].posAndLife.z;
            if (!p.pcisphMode)
            {
                px += vx * p.deltaTime;
                py += vy * p.deltaTime;
                pz += vz * p.deltaTime;
            }

            // 边界碰撞（穿透深度反射，与 GLSL fluid_simulate 一致）
            if (p.useBoundary)
            {
                if (px < p.boundaryMin[0])
                {
                    float pen = p.boundaryMin[0] - px;
                    px        = p.boundaryMin[0] + pen * 0.3f;
                    vx        = fabsf(vx) * 0.3f;
                }
                if (px > p.boundaryMax[0])
                {
                    float pen = px - p.boundaryMax[0];
                    px        = p.boundaryMax[0] - pen * 0.3f;
                    vx        = -fabsf(vx) * 0.3f;
                }
                if (py < p.boundaryMin[1])
                {
                    float pen = p.boundaryMin[1] - py;
                    py        = p.boundaryMin[1] + pen * 0.3f;
                    vy        = fabsf(vy) * 0.3f;
                }
                if (py > p.boundaryMax[1])
                {
                    float pen = py - p.boundaryMax[1];
                    py        = p.boundaryMax[1] - pen * 0.3f;
                    vy        = -fabsf(vy) * 0.3f;
                }
                if (pz < p.boundaryMin[2])
                {
                    float pen = p.boundaryMin[2] - pz;
                    pz        = p.boundaryMin[2] + pen * 0.3f;
                    vz        = fabsf(vz) * 0.3f;
                }
                if (pz > p.boundaryMax[2])
                {
                    float pen = pz - p.boundaryMax[2];
                    pz        = p.boundaryMax[2] - pen * 0.3f;
                    vz        = -fabsf(vz) * 0.3f;
                }
            }

            particles[i].posAndLife.x    = px;
            particles[i].posAndLife.y    = py;
            particles[i].posAndLife.z    = pz;
            particles[i].velAndMaxLife.x = vx;
            particles[i].velAndMaxLife.y = vy;
            particles[i].velAndMaxLife.z = vz;
        }

        void LaunchSPHSimulate(void* particles, const SPHSimulateParams& p, void* stream)
        {
            if (IsCudaPoisoned())
                return;

            cudaStream_t strm = static_cast<cudaStream_t>(stream);
            GPUParticle* devP = static_cast<GPUParticle*>(particles);

            uint32_t blocks = ((uint32_t)p.particleCount + 255) / 256;
            FluidSimulateKernel<<<blocks, 256, 0, strm>>>(devP, p);
            CUDA_CHECK_KERNEL("FluidSimulateKernel");
        }

        // ======================================================================
        // Emit 内核（移植自 fluid_emit.glsl）——初始化整个粒子池
        // ======================================================================

        __global__ static void FluidEmitKernel(GPUParticle* particles, SPHEmitParams p)
        {
            uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
            if (i >= (uint32_t)p.particleCount)
                return;

            // 伪随机偏移（基于粒子索引）
            uint32_t seed  = i * 2654435761u + (uint32_t)(p.time * 1000.0f) * 1099u;
            auto     randf = [&]() -> float
            {
                seed = seed * 747796405u + 2891336453u;
                seed = ((seed >> ((seed >> 28) + 4)) ^ seed) * 277803737u;
                seed = (seed >> 22) ^ seed;
                return (float)seed / 4294967296.0f;
            };

            float ex = (randf() - 0.5f) * 2.0f * p.emitExtents[0];
            float ey = (randf() - 0.5f) * 2.0f * p.emitExtents[1];
            float ez = (randf() - 0.5f) * 2.0f * p.emitExtents[2];

            particles[i].posAndLife.x    = p.emitterPos[0] + ex;
            particles[i].posAndLife.y    = p.emitterPos[1] + ey;
            particles[i].posAndLife.z    = p.emitterPos[2] + ez;
            particles[i].posAndLife.w    = 1.0f; // life = 1（流体粒子永久存活）
            particles[i].velAndMaxLife.x = p.initialVelocity[0];
            particles[i].velAndMaxLife.y = p.initialVelocity[1];
            particles[i].velAndMaxLife.z = p.initialVelocity[2];
            particles[i].velAndMaxLife.w = 1.0f;
            particles[i].params.z        = 0.0f; // density
            particles[i].params.w        = 0.0f; // pressure
        }

        void LaunchSPHEmit(void* particles, const SPHEmitParams& p, void* stream)
        {
            if (IsCudaPoisoned())
                return;

            cudaStream_t strm = static_cast<cudaStream_t>(stream);
            GPUParticle* devP = static_cast<GPUParticle*>(particles);

            uint32_t blocks = ((uint32_t)p.particleCount + 255) / 256;
            FluidEmitKernel<<<blocks, 256, 0, strm>>>(devP, p);
            CUDA_CHECK_KERNEL("FluidEmitKernel");
        }

    } // namespace CudaInterop
} // namespace Engine
