---
paths:
  - "Engine/**"
---

# Engine

静态库目标，构建产物被 Editor 和 Sandbox 链接。

## 目录结构

| 目录 | 职责 |
|------|------|
| `src/Core/` | 应用主循环、Layer、事件系统、输入、窗口 |
| `src/Renderer/` | 渲染抽象层、多 Pass 管线、材质、粒子 |
| `src/Scene/` | ECS（EnTT）、组件、渲染系统 |
| `src/Reflection/` | 编译期反射宏、组件注册、自动序列化/Inspector |
| `src/Asset/` | SlotMap 资源池、异步加载、热重载 |
| `src/Physics/` | Bullet3 物理 + 简易自研碰撞 |
| `src/Audio/` | OpenAL 音频引擎 |
| `src/Media/` | FFmpeg 视频解码（RTSP/文件） |
| `src/Terrain/` | 高度图地形网格生成 |
| `src/Script/` | NativeScript 基类与注册表 |
| `Platform/OpenGL/` | OpenGL 4.3 具体实现 |

## 关键约定

- `Ref<T>` = `std::shared_ptr<T>`，`Scope<T>` = `std::unique_ptr<T>`（定义在 `Core/Base.h`）
- 预编译头 `engpch.h` 由 CMake 自动注入，无需手动 include
- 新组件必须走反射注册流程才能出现在编辑器和序列化中
- 平台相关代码只放在 `Platform/` 下，`src/` 层仅使用抽象接口
