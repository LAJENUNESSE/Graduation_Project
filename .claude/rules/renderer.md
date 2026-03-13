---
paths:
  - "Engine/src/Renderer/**"
---

# Renderer

多 Pass 渲染管线 + 硬件抽象层。

## 渲染管线（SceneRenderer 编排）

1. Shadow Pass → 方向光深度 FBO（PCF 软阴影）
2. Terrain Pass → TerrainPBR splatmap
3. Grass Pass → GPU 草地实例化（Compute → Indirect Draw）
4. Geometry Pass → MeshRenderSystem 提交 RenderPacket 到 RenderQueue
5. Skybox Pass → Cubemap
6. Particle Pass → GPU Compute（emit → simulate → render_args → billboard）
7. Fluid Pass → SPH/PCISPH 流体（密度→力→积分 + SSFR 表面渲染）
8. Post-Processing → ToneMapping + Bloom（亮度提取→高斯模糊→合成） + SSAO + FXAA

## IBL 管线

- Irradiance Map + Prefilter Map + BRDF LUT，全部通过 Compute Shader 生成
- 使用 2D atlas（6 面横向排列）+ `imageLoad` 替代 `samplerCube`（后者在 compute shader 中返回零）
- 纹理**必须**用 `glTexStorage2D`（immutable storage）创建，`glTexImage2D` + imageLoad = 未定义行为

## CUDA Sidecar

- 粒子系统和 SPH 管线可选 CUDA 加速路径（`ENGINE_ENABLE_CUDA=ON`）
- CUDA-OpenGL 互操作：`cudaGraphicsGLRegisterBuffer` 共享 SSBO
- 错误检测 + 全局中毒机制：CUDA 出错后自动回退到 OpenGL Compute 路径

## 核心类

- **RendererAPI**（抽象）→ 具体实现在 `Platform/OpenGL/`
- **RenderQueue** — 延迟命令缓冲，按 Shader 指针排序以减少状态切换
- **Material** = Shader + uniform 缓存 + 纹理绑定，有 dirty flag（`m_Dirty`、`m_TexturesDirty`），`Bind()` 时刷新
- **Framebuffer** — 支持 HDR（RGBA16F）、Entity ID（RED_INTEGER）、深度附件，支持 MSAA 1/2/4/8/16 采样
- **ParticleSystemGPU** — Compute Shader 4 Pass 管线，含 SPH 流体（PCISPH）+ 空间哈希网格

## 注意事项

- Material 的 `Set()` 只缓存值，必须调 `Bind()` 才真正上传到 GPU
- Entity ID 渲染在 Framebuffer 的第二个颜色附件（slot 2），用于鼠标拾取
- 粒子系统使用 Indirect Draw（`DrawArraysIndirect`），对 Mesa/VMware 有 fallback
- Mesh 通过 assimp 加载 GLTF，缓存为 `Ref<Mesh>`
