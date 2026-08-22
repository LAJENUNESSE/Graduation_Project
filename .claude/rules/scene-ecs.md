---
paths:
  - "Engine/src/Scene/**"
---

# Scene（ECS）

基于 EnTT 的实体组件系统，采用 **façade + 服务化** 分层架构。

## 架构概览

```
Scene (façade)
├── SceneEntityIndex          — UUID → entity O(1) 索引
├── ResourceLifecycleCoordinator — 统一资源生命周期回调（Runtime/）
├── SceneEnvironmentState     — Shadow + Skybox 配置
├── SceneRuntimeCoordinator   — 运行时逻辑隔离（物理/脚本/资源，Runtime/）
│   ├── AudioRuntimeStore     — OpenAL Source/Buffer 容器
│   └── VideoRuntimeStore     — FFmpegDecoder/Texture 容器
├── RuntimeComponents.h       — 运行时组件定义（Runtime/）
└── Services (static)
    ├── SceneHierarchyService — 父子关系管理
    └── WorldTransformService — 世界坐标计算（kMaxDepth=64 防层级环）
```

## 核心类

- **Scene** — 轻量 façade，持有 `entt::registry` + 索引/协调器引用，提供 `OnUpdateRuntime()` / `OnUpdateEditor()`
- **Entity** — 轻量包装 `{ entt::entity, Scene* }`，提供 `AddComponent<T>()`、`GetComponent<T>()` 等模板方法
- **Components.h** — 所有组件结构体（POD，必须有默认构造和拷贝构造）
- **SceneRuntimeCoordinator** — 隔离运行时逻辑（物理、脚本），持有 BulletPhysicsWorld
- **ResourceLifecycleCoordinator** — 三类清理回调：EntityCleanup / RuntimeStopCleanup / SceneDestroyCleanup
- **SceneHierarchyService** — 静态工具类，SetParent/RemoveParent/GetChildren/IsAncestorOf/GetRootEntities
- **WorldTransformService** — 静态工具类，ComputeWorldTransform（沿 RelationshipComponent 递归遍历父子链），`kMaxDepth=64` 防御层级环（超限告警并返回本层变换，`WorldTransformService.cpp:22-26`）
- **WorldTransformCache** — 变换缓存辅助（TryGet/Put）

## 渲染系统（Systems/）

静态类，通过 `registry.view<>()` 迭代，入口为静态方法：

| 系统 | 职责 |
|------|------|
| MeshRenderSystem | 提交网格到 RenderQueue |
| ShadowSystem | PCF 阴影贴图 |
| SkyboxSystem | 天空盒渲染 |
| LightSystem | 收集光照数据 |
| TerrainRenderSystem | 地形 + Splatmap |
| GrassRenderSystem | GPU 草地实例化 |
| AudioSystem | 音频播放（使用 AudioRuntimeStore） |
| VideoSystem | FFmpeg 视频流（使用 VideoRuntimeStore） |

## 注意事项

- 每个组件结构体必须有默认构造函数和拷贝构造函数
- `Scene::OnRuntimeStart/Stop` 委托给 SceneRuntimeCoordinator
- `Scene::DestroyEntity()` 递归销毁子实体（`std::move` 取出 Children 再递归，move 后原容器为空故无需从父中移除自身；见 `Scene.cpp:47-59`），先调 `m_RuntimeCoordinator->DestroyPhysicsBody` 与 `m_LifecycleCoordinator.PreDestroyEntity`（触发 EntityCleanup 回调）再销毁实体
- 物理后端：Bullet3（`BulletPhysicsWorld`），切换场景时完整清理
- Scene 是 façade — 新功能应考虑提取为 Service 或 Coordinator，不要直接膨胀 Scene 类
- SceneHierarchyService / WorldTransformService / SceneEntityIndex 已有单元测试覆盖，修改时需确保测试通过
