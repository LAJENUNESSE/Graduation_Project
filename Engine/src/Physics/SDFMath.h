#pragma once

// SDF 纯数学函数 —— header-only，兼容 CUDA (__device__) 和 C++。
// 从 CudaSPHPipeline.cu 提取以便单元测试。

#include <cmath>

#ifdef __CUDACC__
#define SDF_DEVICE __device__ __host__
#else
#define SDF_DEVICE
#endif

namespace Engine
{
    namespace SDFMath
    {

        struct SDFResult
        {
            float sdf;
            float normalX, normalY, normalZ;
        };

        // Box SDF：计算点到 AABB 的有符号距离（局部坐标系）
        // halfExtents: 盒子半尺寸
        // lx/ly/lz: 点在盒子局部坐标系中的位置
        SDF_DEVICE inline SDFResult BoxSDF(float lx, float ly, float lz, float hex, float hey, float hez)
        {
            SDFResult r{};

            float ddx = std::abs(lx) - hex;
            float ddy = std::abs(ly) - hey;
            float ddz = std::abs(lz) - hez;

            float ox = (ddx > 0.0f) ? ddx : 0.0f;
            float oy = (ddy > 0.0f) ? ddy : 0.0f;
            float oz = (ddz > 0.0f) ? ddz : 0.0f;

            float inner = ddx;
            if (ddy > inner)
                inner = ddy;
            if (ddz > inner)
                inner = ddz;
            if (inner > 0.0f)
                inner = 0.0f;

            r.sdf = std::sqrt(ox * ox + oy * oy + oz * oz) + inner;

            // 法线：沿穿透最深轴
            float sx = (lx >= 0.0f) ? 1.0f : -1.0f;
            float sy = (ly >= 0.0f) ? 1.0f : -1.0f;
            float sz = (lz >= 0.0f) ? 1.0f : -1.0f;

            if (ddx > ddy && ddx > ddz)
            {
                r.normalX = sx;
                r.normalY = 0.0f;
                r.normalZ = 0.0f;
            }
            else if (ddy > ddz)
            {
                r.normalX = 0.0f;
                r.normalY = sy;
                r.normalZ = 0.0f;
            }
            else
            {
                r.normalX = 0.0f;
                r.normalY = 0.0f;
                r.normalZ = sz;
            }

            return r;
        }

        // Sphere SDF：计算点到球心的有符号距离
        // lx/ly/lz: 点在球局部坐标系中的位置（球心 = 原点）
        // radius: 球半径
        SDF_DEVICE inline SDFResult SphereSDF(float lx, float ly, float lz, float radius)
        {
            SDFResult r{};

            float lenL = std::sqrt(lx * lx + ly * ly + lz * lz);
            r.sdf      = lenL - radius;

            if (lenL > 0.001f)
            {
                r.normalX = lx / lenL;
                r.normalY = ly / lenL;
                r.normalZ = lz / lenL;
            }
            else
            {
                r.normalX = 0.0f;
                r.normalY = 1.0f;
                r.normalZ = 0.0f;
            }

            return r;
        }

    } // namespace SDFMath
} // namespace Engine

#undef SDF_DEVICE
