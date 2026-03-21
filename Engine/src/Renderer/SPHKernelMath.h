#pragma once

// SPH 核函数系数的纯数学计算 —— header-only inline，不依赖引擎。
// 从 SPHCommon.cpp 提取以便单元测试。

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Engine
{

    struct SPHKernelParams;

    namespace SPHKernelMath
    {
        struct KernelCoeffs
        {
            float h;
            float h2;
            float h6;
            float h9;
            float poly6Coeff;
            float spikyCoeff;
        };

        inline KernelCoeffs Compute(float smoothingRadius)
        {
            KernelCoeffs p;
            p.h          = smoothingRadius;
            p.h2         = p.h * p.h;
            p.h6         = p.h2 * p.h2 * p.h2;
            p.h9         = p.h6 * p.h2 * p.h;
            p.poly6Coeff = 315.0f / (64.0f * static_cast<float>(M_PI) * p.h9);
            p.spikyCoeff = -45.0f / (static_cast<float>(M_PI) * p.h6);
            return p;
        }

        /// 按 Solenthaler 2009 论文推导 PCISPH δ
        /// 假设初始粒子间距 ≈ (mass / restDensity)^(1/3)（立方格点排列）
        inline float ComputePCISPHDelta(float smoothingRadius, float particleMass, float restDensity, float dt)
        {
            float spacing = std::cbrt(particleMass / restDensity);
            float h       = smoothingRadius;
            float h6      = h * h * h * h * h * h;
            float pi      = static_cast<float>(M_PI);

            // 在立方格点上枚举 [-h..h] 范围内的邻居，累加 Spiky 梯度
            float sumGradWx = 0.0f, sumGradWy = 0.0f, sumGradWz = 0.0f;
            float sumDotGradW = 0.0f;

            int range = static_cast<int>(std::ceil(h / spacing));
            for (int iz = -range; iz <= range; iz++)
                for (int iy = -range; iy <= range; iy++)
                    for (int ix = -range; ix <= range; ix++)
                    {
                        if (ix == 0 && iy == 0 && iz == 0)
                            continue;
                        float rx   = ix * spacing;
                        float ry   = iy * spacing;
                        float rz   = iz * spacing;
                        float dist = std::sqrt(rx * rx + ry * ry + rz * rz);
                        if (dist >= h || dist < 1e-6f)
                            continue;
                        // Spiky 梯度: -45/(πh⁶) × (h-r)² × r̂
                        float coeff = -45.0f / (pi * h6);
                        float diff  = h - dist;
                        float invD  = 1.0f / dist;
                        float gx    = coeff * diff * diff * rx * invD;
                        float gy    = coeff * diff * diff * ry * invD;
                        float gz    = coeff * diff * diff * rz * invD;
                        sumGradWx += gx;
                        sumGradWy += gy;
                        sumGradWz += gz;
                        sumDotGradW += gx * gx + gy * gy + gz * gz;
                    }

            float sumGradW2 = sumGradWx * sumGradWx + sumGradWy * sumGradWy + sumGradWz * sumGradWz;
            float beta      = dt * dt * particleMass * particleMass * (sumGradW2 + sumDotGradW);
            if (std::abs(beta) < 1e-12f)
                return 0.3f; // fallback
            return -1.0f / beta;
        }
    } // namespace SPHKernelMath

} // namespace Engine
