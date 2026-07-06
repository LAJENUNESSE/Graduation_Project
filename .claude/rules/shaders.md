---
paths:
  - "assets/shaders/**"
---

# Shaders

GLSL 着色器文件，由 `Shader::Create()` 加载。OpenGL 直接消费 GLSL，Vulkan 通过 **shaderc** 运行时编译为 SPIR-V。

## 文件格式

单文件内通过 `#type` 指令分隔不同着色器阶段：

```glsl
#type vertex
#version 330 core
layout(location = 0) in vec3 a_Position;
// ...

#type fragment
#version 330 core
out vec4 FragColor;
// ...

#type compute
#version 430 core
layout(local_size_x = 256) in;
// ...
```

## 命名规范

| 前缀/模式 | 用途 | 示例 |
|-----------|------|------|
| PBR / TerrainPBR | PBR 材质着色器 | `PBR.glsl`, `TerrainPBR.glsl` |
| particle_* | GPU 粒子系统 | `particle_emit.glsl`, `particle_simulate.glsl` |
| grass_* | GPU 草地 | `grass_placement.glsl`, `grass_billboard.glsl` |
| sph_* / fluid_* | SPH 流体模拟 | `sph_density.glsl`, `fluid_composite.glsl` |
| sph_pcisph_* | PCISPH 变体 | `sph_pcisph_predict.glsl` |
| grid_* | 空间哈希网格 | `grid_hash.glsl`, `grid_prefix_sum.glsl`, `grid_scatter.glsl` |
| IBL_* | IBL 预计算 | `IBL_Irradiance.glsl`, `IBL_Prefilter.glsl`, `IBL_BRDF_LUT.glsl` |
| 后处理 | 屏幕空间效果 | `SSAO.glsl`, `GaussianBlur.glsl`, `ToneMapping.glsl` |

## 顶点布局约定

- `location 0` = position
- `location 1` = normal
- `location 2` = texcoord
- `location 3` = tangent

## OpenGL / Vulkan 双路径（`#ifdef VULKAN`）

迁移到 Vulkan compute 的 shader 在同一文件内并存两套绑定语法，`VulkanShader.cpp` 在 shaderc 编译选项里显式注入 `VULKAN=1` macro：

```glsl
#ifdef VULKAN
// Vulkan：SSBO 显式 set=0，default uniform 改用 push_constant
layout(std430, set = 0, binding = 0) readonly buffer ParticlePool { ... };
layout(push_constant) uniform PushConstants {
    int   u_AliveCount;
    float u_CellSize;
} pc;
#define u_AliveCount  pc.u_AliveCount
#define u_CellSize    pc.u_CellSize
#else
// OpenGL：传统 binding + default uniform
layout(std430, binding = 0) readonly buffer ParticlePool { ... };
uniform int   u_AliveCount;
uniform float u_CellSize;
#endif
```

约定：
- **OpenGL 分支保持不变**，main 分支行为零回归
- **push_constant 限于 ≤128 bytes 的高频小常量**（cell_count / roughness / particle_count 等），避免新建 UBO（决策 D-1）
- 已迁移到 `#ifdef VULKAN` 双路径：`grid_hash`、`grid_prefix_sum`、`grid_scatter`、`IBL_BRDF_LUT`、`IBL_Irradiance`、`IBL_Prefilter`
- 未迁移（仅 OpenGL）：particle_*、sph_*、fluid_*

## 注意事项

- GLSL 版本：渲染着色器用 330（OpenGL 3.3），Compute Shader 用 430+（OpenGL 4.3）
- `#type` 指令必须在 `#version` 之前；`#pragma`/`#define` 放在 `#type` 之后
- Compute Shader 的 SSBO 用 `layout(std430, binding=N) buffer`，结构体打包需与 C++ 逐字节对齐（注意 padding）
- Compute 多 Pass 之间需要 `memoryBarrier()` + `barrier()` 同步（Vulkan 路径由 `VulkanBarrierUtil::ResolveBarrierBits` 翻译成 `vkCmdPipelineBarrier`）
- Indirect Draw 的 command buffer 结构必须匹配 `DrawArraysIndirectCommand`
- 共享内存需显式声明大小：`shared float data[256];`
- 修改双路径 shader 时**两侧必须同步**改动，禁止只动一边后让另一边静默偏离
