---
paths:
  - "Engine/src/Asset/**"
---

# Asset（资源管理）

基于 SlotMap 的泛型资源池 + 异步加载 + 热重载。

## 核心类

- **AssetManager** — 静态单例；模板特化支持 Texture2D、Mesh、TextureCubemap
- **AssetHandle** — `{ Index, Generation }` 安全句柄，Generation 不匹配时视为失效
- **SlotMap** — 分代竞技场分配器，支持空洞复用 + Generation 计数
- **AsyncLoadQueue** — 后台加载线程，返回 1x1 灰色占位纹理，加载完成后自动替换
- **FileWatcher** — 文件系统监听，检测变更后调用 `ReloadAsset()` 热重载

## 使用方式

```cpp
// 同步加载
AssetHandle h = AssetManager::Load<Texture2D>("assets/textures/albedo.png");
Texture2D* tex = AssetManager::Get<Texture2D>(h);

// 异步加载（仅纹理）
AssetHandle h = AssetManager::LoadAsync<Texture2D>("assets/textures/albedo.png");
// 立即返回占位纹理，后台完成后自动替换
```

## 注意事项

- 必须先调 `AssetManager::Init()` 再加载任何资源
- 每帧调 `AssetManager::Update(deltaTime)` 以处理异步回调
- 异步加载仅支持纹理，Mesh 始终同步
- 路径相对于项目根目录，由 `EntryPoint.h` 自动设定工作目录
- 维护 `s_TexturePathIndex` / `s_MeshPathIndex` 做路径去重，避免重复加载
