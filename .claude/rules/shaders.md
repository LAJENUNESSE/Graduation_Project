---
paths:
  - "assets/shaders/**"
---

# Shaders

GLSL 着色器文件，由 `Shader::Create()` 加载。

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
| sph_* / fluid_* | SPH 流体模拟 | `sph_density.glsl`, `fluid_render.glsl` |
| sph_pcisph_* | PCISPH 变体 | `sph_pcisph_predict.glsl` |
| grid_* | 空间哈希网格 | `grid_hash.glsl`, `grid_prefix_sum.glsl` |

## 顶点布局约定

- `location 0` = position
- `location 1` = normal
- `location 2` = texcoord
- `location 3` = tangent

## 注意事项

- GLSL 版本：渲染着色器用 330（OpenGL 3.3），Compute Shader 用 430+（OpenGL 4.3）
- `#type` 指令必须在 `#version` 之前；`#pragma`/`#define` 放在 `#type` 之后
- Compute Shader 的 SSBO 用 `layout(std430, binding=N) buffer`，结构体打包需与 C++ 逐字节对齐（注意 padding）
- Compute 多 Pass 之间需要 `memoryBarrier()` + `barrier()` 同步
- Indirect Draw 的 command buffer 结构必须匹配 `DrawArraysIndirectCommand`
- 共享内存需显式声明大小：`shared float data[256];`
