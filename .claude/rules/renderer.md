---
paths:
  - "Engine/src/Renderer/**"
---

# Renderer

多 Pass 渲染管线 + 硬件抽象层。

## 渲染管线（SceneRenderer 编排）

1. Shadow Pass → 方向光深度 FBO（PCF 软阴影）
2. Geometry Pass → MeshRenderSystem 提交 RenderPacket 到 RenderQueue
3. Terrain Pass → TerrainPBR splatmap
4. Skybox Pass → Cubemap
5. Particle Pass → GPU Compute（emit → simulate → render_args → billboard）
6. Post-Processing → ToneMapping + Bloom

## 核心类

- **RendererAPI**（抽象）→ 具体实现在 `Platform/OpenGL/`
- **RenderQueue** — 延迟命令缓冲，按 Shader 指针排序以减少状态切换
- **Material** = Shader + uniform 缓存 + 纹理绑定，有 dirty flag（`m_Dirty`、`m_TexturesDirty`），`Bind()` 时刷新
- **Framebuffer** — 支持 HDR（RGBA16F）、Entity ID（RED_INTEGER）、深度附件
- **ParticleSystemGPU** — Compute Shader 4 Pass 管线，含 SPH 流体（PCISPH）+ 空间哈希网格

## 注意事项

- Material 的 `Set()` 只缓存值，必须调 `Bind()` 才真正上传到 GPU
- Entity ID 渲染在 Framebuffer 的第二个颜色附件（slot 2），用于鼠标拾取
- 粒子系统使用 Indirect Draw（`DrawArraysIndirect`），对 Mesa/VMware 有 fallback
- Mesh 通过 assimp 加载 GLTF，缓存为 `Ref<Mesh>`
