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
- **AsyncLoadQueue** — 后台加载线程，返回 1x1 灰色占位纹理，加载完成后自动替换；加载失败回退 1x1 品红占位（`AsyncLoadQueue.cpp:90`）
- **FileWatcher** — 文件系统监听，`AssetManager::Update()` 内部经 `CheckChanges()` 触发 `ReloadAsset()` 热重载（`AssetManager.cpp:125-132`）

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
- 维护 `s_TexturePathIndex` / `s_MeshPathIndex` / `s_CubemapPathIndex` 做路径去重，避免重复加载
- handle 自身 Type 字段携带类型信息，取代旧 `s_HandleTypes` 全局 map（三个 SlotMap 曾产生相同 {Index, Generation} 导致类型断言失败，见 `AssetManager.h:62-64` 注释）
- SlotMap `Remove()` 会屏蔽保留槽 index=0，避免误删
- `AssetHandle::IsValid()` 检查 index + generation 双重验证
