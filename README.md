# Graduation Project — 3D Game Engine & Editor

湖北第二师范学院毕业设计：基于 C++17 / OpenGL 4.3 的 3D 游戏引擎与可视化编辑器。

## Features

### 渲染

- **PBR 渲染管线**：OpenGL 4.3 Core Profile，基于物理的渲染，金属度/粗糙度工作流
- **IBL 环境光照**：Compute Shader 生成 Irradiance Map + Prefilter Map + BRDF LUT，实现基于图像的全局光照
- **光照与阴影**：方向光、点光源、聚光灯，方向光 Shadow Map + PCF 3×3 软阴影
- **材质系统**：漫反射/法线/金属度/粗糙度/AO 纹理，纹理平铺控制
- **后处理管线**：HDR 色调映射、Bloom 辉光（半分辨率优化）、SSAO 屏幕空间环境光遮蔽、Gamma 校正、MSAA 抗锯齿（1/2/4/8/16x）
- **天空盒**：六面 Cubemap 天空盒渲染
- **多 Pass 渲染**：Shadow → Terrain → Grass → Geometry → Skybox → Particle → Fluid + PostProcessing（8 Pass）

### 模拟

- **GPU 粒子系统**：Compute Shader 驱动的粒子发射/模拟/渲染管线，支持可选 CUDA 加速路径
- **SPH 流体模拟**：PCISPH 算法 + Screen Space Fluid Rendering（厚度/深度/高斯平滑/合成），支持可选 CUDA 加速路径
- **CUDA Sidecar**：粒子和 SPH 管线可通过 CUDA 加速，含错误检测 + 全局中毒回退到 OpenGL Compute
- **GPU 草地渲染**：Compute Shader 在地形高度图上放置草叶，Billboard 渲染 + 风力动画
- **物理系统**：Bullet3 刚体物理（动态/静态/运动学），碰撞检测（Box/Sphere/Mesh），物理调试可视化
- **地形系统**：高度图驱动的地形生成，多层 Splatmap 纹理混合，多级 LOD

### 系统

- **ECS 架构**：基于 EnTT 的实体-组件-系统，21 种组件类型
- **反射系统**：编译期宏 + 运行时 ComponentRegistry，驱动自动序列化与自动属性面板
- **资产管理**：SlotMap 资源池 + Handle 安全引用，异步加载 + FileWatcher 热重载
- **场景序列化**：YAML 格式保存/加载，反射驱动自动序列化
- **音频系统**：OpenAL 3D 空间化音频，支持 WAV 播放
- **视频流播放**：FFmpeg 解码 + RTSP/RTMP 网络流接入，后台异步连接，三重缓冲帧传递
- **脚本系统**：NativeScript 原生 C++ 脚本绑定

### 编辑器

- **可视化编辑器**：ImGui Docking 布局 + ImGuizmo 变换操纵器
- **面板系统**：场景层级 / 属性检查器 / 控制台 / 资源浏览器 / 渲染设置
- **撤销/重做**：双栈 UndoSystem，支持 Transform / Entity / Property / Parent 等 6 种命令
- **性能监测**：GPU Timer Query + 帧时间分析
- **崩溃处理**：自动崩溃捕获与报告

## Build

### 前置准备

```bash
# 1. 初始化 Git 子模块
git submodule update --init --recursive

# 2. 安装 vcpkg（首次）
git clone https://github.com/microsoft/vcpkg.git <路径>
<路径>/bootstrap-vcpkg.bat                              # Windows
<路径>/bootstrap-vcpkg.sh                               # Linux
<路径>/vcpkg install ffmpeg openal-soft --triplet x64-windows  # 或 x64-linux
```

### Windows（VS 2022 Build Tools + vcpkg）

项目提供了 CMake Presets，按需选择：

```bash
# 配置（二选一）
cmake --preset default          # 无 CUDA
cmake --preset vs2022-cuda      # 有 CUDA（需安装 NVIDIA CUDA Toolkit）

# 构建
cmake --build build --config RelWithDebInfo --target Editor
```

### Linux（Ninja + vcpkg）

```bash
# 配置（二选一）
cmake --preset linux-default    # 无 CUDA
cmake --preset linux-cuda       # 有 CUDA

# 构建
cmake --build build --target Editor
```

### Run

```bash
# Windows
./build/Editor/RelWithDebInfo/Editor.exe

# Linux
./build/Editor/Editor.exe
```

编辑器可从任意目录启动，会自动检测项目根目录。

## Dependencies

| Library | Purpose |
|---------|---------|
| [GLFW](https://github.com/glfw/glfw) | 窗口管理与输入 |
| [GLAD](https://gen.glad.sh/) | OpenGL 函数加载 |
| [GLM](https://github.com/g-truc/glm) | 数学库 |
| [EnTT](https://github.com/skypjack/entt) | Entity-Component-System |
| [spdlog](https://github.com/gabime/spdlog) | 日志系统 |
| [Dear ImGui](https://github.com/ocornut/imgui) (docking) | 编辑器 UI |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | 3D 变换操纵器 |
| [yaml-cpp](https://github.com/jbeder/yaml-cpp) | 场景序列化 |
| [stb_image](https://github.com/nothings/stb) | 图片加载 |
| [Bullet3](https://github.com/bulletphysics/bullet3) | 物理引擎 |
| [Assimp](https://github.com/assimp/assimp) | 3D 模型导入 |
| [tinyfiledialogs](https://sourceforge.net/projects/tinyfiledialogs/) | 文件对话框 |
| [FFmpeg](https://ffmpeg.org/) | 视频解码（vcpkg） |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | 音频引擎（vcpkg） |
| [NVIDIA CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit) | GPU 加速（可选） |

## Architecture

```
┌──────────────────────────────────────────────────────┐
│  Editor (exe)                                        │
│  EditorLayer → Session / Shell / Panels / Render /   │
│                Viewport / Gizmo Controllers          │
│                + UndoSystem                          │
├──────────────────────────────────────────────────────┤
│  Engine (static lib)                                 │
│  ┌────────┬──────────┬───────────┬─────────────────┐ │
│  │ Scene  │ Renderer │ Reflection│ Asset           │ │
│  │(façade)│(多 Pass) │(自动序列化)│(SlotMap+Handle) │ │
│  ├────────┼──────────┼───────────┼─────────────────┤ │
│  │Physics │ Audio    │ Media     │ Terrain/Script  │ │
│  │(Bullet)│ (OpenAL) │ (FFmpeg)  │                 │ │
│  └────────┴──────────┴───────────┴─────────────────┘ │
├──────────────────────────────────────────────────────┤
│  Platform                                            │
│  OpenGL 4.3          │  CUDA Sidecar (可选)          │
│  (渲染 + Compute)     │  (粒子 + SPH 加速，含回退)    │
└──────────────────────────────────────────────────────┘
```

## Project Structure

```
Engine/                     核心引擎静态库
  src/
    Core/                   应用主循环、窗口、输入、日志、崩溃处理
    Renderer/               SceneRenderer 多 Pass 管线、PBR、IBL、后处理、粒子、流体
    Scene/                  ECS 场景图（façade + 服务化）、21 种组件、运行时协调器
    Reflection/             编译期反射、ComponentRegistry、自动序列化/Inspector
    Asset/                  资产管理器（SlotMap + Handle）、异步加载、FileWatcher 热重载
    Physics/                Bullet3 物理世界、调试可视化
    Audio/                  OpenAL 音频引擎
    Media/                  FFmpeg 视频解码器
    Terrain/                高度图地形生成
    Script/                 NativeScript 脚本系统
    Events/                 事件系统（发布-订阅）
    Debug/                  PerformanceMonitor、GPU Timer Query
    ImGui/                  ImGui 后端集成
  Platform/
    OpenGL/                 OpenGL 4.3 后端实现
    CUDA/                   CUDA 加速管线（粒子 + SPH）、GL 互操作、错误回退

Editor/                     可视化编辑器
  src/
    EditorLayer             主协调器
    EditorSceneSession      场景生命周期（Edit ↔ Play）
    EditorShell             Dockspace + 快捷键
    EditorRenderController  SceneRenderer + 后处理
    EditorViewportController  EditorCamera + Framebuffer
    UndoSystem              撤销/重做（双栈 6 种命令）
    Panels/                 层级 / 属性 / 控制台 / 资源浏览器 / 渲染设置

assets/                     资源文件
  shaders/                  36 个 GLSL 着色器（PBR/IBL/后处理/粒子/SPH/SSFR/草地/阴影/地形）
  scenes/                   场景文件（YAML）
  models/                   3D 模型
  textures/                 纹理资源（含 Skybox cubemaps）
  audio/                    音频资源
  terrain/                  地形高度图

vendor/                     第三方依赖（Git 子模块）
docs/                       技术调研报告
```

## CI/CD

GitHub Actions 自动化流程：

- **构建验证**：Windows (MSVC) + Linux (GCC) + Linux + CUDA，ccache 加速增量构建
- **安全分析**：CodeQL 每周定时扫描（不阻塞 push/PR）

## License

This project is licensed under the **GNU Lesser General Public License v2.1+** (LGPL v2.1+).

### Commercial Use & FFmpeg

Since FFmpeg is linked as an **external dynamic library** (`.dll` on Windows), you can:
- ✅ Use this engine in **proprietary/commercial applications**
- ✅ Distribute closed-source products
- ✅ Users can replace FFmpeg with their own build if needed

**Requirements:**
- Distribute the LGPL-licensed engine source code or provide dynamic linking capability
- Include the `LICENSE` file with your distribution
- Allow FFmpeg replacement by users (e.g., ship FFmpeg as a separate DLL)

### Dependency Licenses

| Dependency | License | Status |
|---|---|---|
| FFmpeg | GPL v2 | Dynamic DLL (external, replaceable) |
| GLFW, Bullet3 | zlib | ✓ |
| EnTT, spdlog, ImGui, yaml-cpp | MIT | ✓ |
| Assimp | BSD 3-Clause | ✓ |
| OpenAL Soft | LGPL/MIT | ✓ |

See `LICENSE` file for full details.
