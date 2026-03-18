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
    } // namespace SPHKernelMath

} // namespace Engine
