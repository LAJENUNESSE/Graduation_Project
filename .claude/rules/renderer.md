---
paths:
  - "Engine/src/Renderer/**"
---

# Renderer

多 Pass 渲染管线 + 硬件抽象层（OpenGL 4.3 默认，Vulkan 1.2+ 可选）。

## 渲染管线（SceneRenderer 编排）

1. Shadow Pass → 方向光深度 FBO（PCF 软阴影）
2. Terrain Pass → TerrainPBR splatmap
3. Grass Pass → GPU 草地实例化（Compute → Indirect Draw）
4. Geometry Pass → MeshRenderSystem 提交 RenderPacket 到 RenderQueue
5. Skybox Pass → Cubemap
6. Particle Pass → GPU Compute（emit → simulate → render_args → billboard），支持 GPU 刚体碰撞数据上传
7. Fluid Pass → SPH/PCISPH 流体（密度→力→积分 + SSFR 表面渲染）
8. Post-Processing → ToneMapping + Bloom（亮度提取→高斯模糊→合成）+ SSAO + FXAA + MSAA resolve

## IBL 管线

- Irradiance Map + Prefilter Map + BRDF LUT，全部通过 Compute Shader 生成
- 使用 2D atlas（6 面横向排列）+ `imageLoad` 替代 `samplerCube`（后者在 compute shader 中返回零）
- 纹理**必须**用 `glTexStorage2D`（immutable storage）创建，`glTexImage2D` + imageLoad = 未定义行为
- Vulkan 路径：`VulkanIBLGenerator` 用 3 个 compute dispatch 完成，目前用占位 env atlas（待 `VulkanTextureCubemap` 实装）

## 后端抽象

- 抽象层（本目录）— RendererAPI / RenderCommand / Shader / Texture / Framebuffer / Buffer / StorageBuffer / UniformBuffer / VertexArray / IBLGenerator / GPUAsyncReadback / RendererCapabilities
- OpenGL 实现：`Platform/OpenGL/`（默认，详见 `opengl.md`）
- Vulkan 实现：`Platform/Vulkan/`（可选，已合并主分支，详见 `vulkan.md`，`--vulkan` 启用）
- **抽象层已封堵**: `Engine/src/` 下零 `#include <glad/gl.h>`，所有 GL 调用必须经 `RendererAPI` 抽象
- 抽象泄漏案例：SSAO 噪声、blend state save/restore、异步回读、能力查询、IBL 生成已全部抽象到 Platform 层

## SPHKernelMath（header-only）

从 SPHCommon 提取的 SPH 核函数系数纯数学计算，不依赖引擎，可独立单元测试。

## 核心类

- **RendererAPI**（抽象）→ `Platform/OpenGL/` 或 `Platform/Vulkan/`
- **RenderCommand** — 静态转发，所有调用经 `s_RendererAPI->xxx()`
- **RenderQueue** — 延迟命令缓冲，按 Shader 指针排序以减少状态切换
- **Material** = Shader + uniform 缓存 + 纹理绑定，有 dirty flag（`m_Dirty`、`m_TexturesDirty`），`Bind()` 时刷新
- **Framebuffer** — 支持 HDR（RGBA16F）、Entity ID（RED_INTEGER）、深度附件，支持 MSAA 1/2/4/8/16 采样
- **ParticleSystemGPU** — Compute Shader 4 Pass 管线，含 GPU 刚体上传 + 空间哈希网格
- **FluidSystemGPU** — SPH/PCISPH 流体管线，自适应 grid 重建
- **SpatialHashGrid** — hash / prefix_sum (三 pass) / scatter，Vulkan 路径要求调用方 `SetExternalBuffers(...)` 注入外部 buffer
- **GPUAsyncReadback** — fence + persistent map 异步回读抽象（OpenGL + Vulkan 均已实现）
- **SPHCommon** — SPH 通用参数和配置
- **IBLGenerator** — Irradiance/Prefilter/BRDF LUT compute 生成

## 注意事项

- Material 的 `Set()` 只缓存值，必须调 `Bind()` 才真正上传到 GPU
- Entity ID 渲染在 Framebuffer 的第二个颜色附件（slot 2），用于鼠标拾取
- 粒子系统使用 Indirect Draw（`DrawArraysIndirect`），对 Mesa/VMware 有 fallback
- Mesh 通过 assimp 加载 GLTF，缓存为 `Ref<Mesh>`
- SPH 流体 WCSPH 模式对负压力做 clamp 处理
- PCISPH 变体在预测步骤后需恢复预测位置
- 新增纯数学函数应提取到 header-only 文件（如 SPHKernelMath.h），保持可测试性
- 添加新的 GPU 资源/状态调用时，必须同步设计 OpenGL + Vulkan 两路径，禁止仅写一边
