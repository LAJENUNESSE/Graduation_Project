---
paths:
  - "Engine/src/Scene/**"
---

# Scene（ECS）

基于 EnTT 的实体组件系统。

## 核心类

- **Scene** — 持有 `entt::registry`、视口大小、物理世界；提供 `OnUpdateRuntime()` / `OnUpdateEditor()`
- **Entity** — 轻量包装 `{ entt::entity, Scene* }`，提供 `AddComponent<T>()`、`GetComponent<T>()` 等模板方法
- **Components.h** — 所有组件结构体（POD，必须有默认构造和拷贝构造）

## 渲染系统（Systems/）

静态类，通过 `registry.view<>()` 迭代，入口为静态方法 `Render(Scene& scene)`：

| 系统 | 职责 |
|------|------|
| MeshRenderSystem | 提交网格到 RenderQueue |
| ShadowSystem | PCF 阴影贴图 |
| SkyboxSystem | 天空盒渲染 |
| LightSystem | 收集光照数据 |
| TerrainRenderSystem | 地形 + Splatmap |
| GrassRenderSystem | GPU 草地实例化 |
| AudioSystem | 音频播放 |
| VideoSystem | FFmpeg 视频流 |

## 注意事项

- 每个组件结构体必须有默认构造函数和拷贝构造函数
- `Scene::OnRuntimeStart/Stop` 触发 NativeScript 生命周期
- `Scene::DestroyEntity()` 必须先清理 Bullet 物理体
- 物理后端可选（Bullet 或自研），不能混用
