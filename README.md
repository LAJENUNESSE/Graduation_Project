# 3D Game Engine & Editor

基于 C++20 / OpenGL 4.3 的 3D 游戏引擎与可视化编辑器，支持可选 Vulkan 渲染后端、CUDA 粒子/流体加速，以及服务器权威的局域网双玩家同步演示。

## Features

### 渲染

- **PBR 渲染管线**：OpenGL 4.3 Core Profile，基于物理的渲染，金属度/粗糙度工作流
- **IBL 环境光照**：Compute Shader 生成 Irradiance Map + Prefilter Map + BRDF LUT，实现基于图像的全局光照
- **光照与阴影**：方向光、点光源、聚光灯，方向光 Shadow Map + PCF 3×3 软阴影
- **材质系统**：漫反射/法线/金属度/粗糙度/AO 纹理，纹理平铺控制
- **后处理管线**：HDR 色调映射、Bloom 辉光（半分辨率优化）、SSAO 屏幕空间环境光遮蔽、Gamma 校正、FXAA、MSAA 抗锯齿（1/2/4/8/16x）
- **天空盒**：六面 Cubemap 天空盒渲染
- **多 Pass 渲染**：Shadow → Terrain → Grass → Geometry → Skybox → Particle → Fluid → PostProcessing
- **Vulkan 后端**：可选 Vulkan 1.2+ 渲染实现，通过 CMake preset 编译并在启动时切换

### 模拟

- **GPU 粒子系统**：Compute Shader 驱动的粒子发射/模拟/渲染管线，支持可选 CUDA 加速路径
- **SPH 流体模拟**：PCISPH 算法 + Screen Space Fluid Rendering（厚度/深度/高斯平滑/合成），支持可选 CUDA 加速路径
- **CUDA Sidecar**：粒子和 SPH 管线可通过 CUDA 加速，含错误检测 + 全局中毒回退到 OpenGL Compute；默认关闭，可通过专用 preset 启用
- **GPU 草地渲染**：Compute Shader 在地形高度图上放置草叶，Billboard 渲染 + 风力动画
- **物理系统**：Bullet3 刚体物理（动态/静态/运动学），碰撞检测（Box/Sphere/Mesh），物理调试可视化
- **地形系统**：高度图驱动的地形生成，多层 Splatmap 纹理混合，多级 LOD

### 系统

- **ECS 架构**：基于 EnTT 的实体-组件-系统，数据组件通过反射注册接入编辑器与序列化
- **反射系统**：编译期宏 + 运行时 ComponentRegistry，驱动自动序列化与自动属性面板
- **资产管理**：SlotMap 资源池 + Handle 安全引用，异步加载 + FileWatcher 热重载
- **场景序列化**：YAML 格式保存/加载，反射驱动自动序列化
- **音频系统**：OpenAL 3D 空间化音频，支持 WAV 播放
- **视频流播放**：FFmpeg 解码 + RTSP/RTMP 网络流接入，后台异步连接，三重缓冲帧传递
- **脚本系统**：支持 NativeScript 原生 C++ 脚本与 Lua 脚本；C++ 示例见 `Editor/src/Scripts/`
- **网络同步**：独立无头 `Server` 进程以固定步长运行权威场景和物理，多个 Editor 客户端通过 TCP 交换输入与 Transform 快照；当前面向局域网双玩家演示，不含客户端预测/回滚及流体同步

### 编辑器

- **可视化编辑器**：ImGui Docking 布局 + ImGuizmo 变换操纵器
- **面板系统**：场景层级 / 属性检查器 / 控制台 / 资源浏览器 / 渲染设置 / 脚本编辑器
- **撤销/重做**：双栈 UndoSystem，支持 Transform / Entity / Property / Parent 等编辑命令
- **性能监测**：GPU Timer Query + 帧时间分析
- **自动保存与恢复**：定时自动保存 + 崩溃/异常退出后自动恢复，关闭前未保存变更提示
- **崩溃处理**：自动崩溃捕获与报告

## Build

### 首次配置（新机器）

```powershell
# 自动安装 Git、VS Build Tools、vcpkg 及项目依赖（需管理员权限，脚本会自动提权）
./setup.ps1

# 已有完整 VS 2022 的用户可跳过 Build Tools 安装
./setup.ps1 -SkipVS
```

配置完成后重启终端，然后运行下方构建命令。

> **首次构建提示**：项目依赖多个 Git 子模块和 vcpkg 包（Asio、FFmpeg、OpenAL Soft、Lua）。首次构建需下载并编译较多依赖，建议使用高速网络或代理；后续增量构建不再重复下载。

### Windows 一键构建

```powershell
# 基本构建（自动处理子模块、vcpkg、CMake 配置与构建）
./build.ps1

# 指定构建配置
./build.ps1 -Config Debug
```

脚本会自动检测 vcpkg 和 CMake，找不到时自动安装 vcpkg。

### 手动构建

#### 前置准备

```bash
# 1. 克隆项目（推荐浅克隆子模块，节省时间和带宽）
git clone --recursive --shallow-submodules <repo-url>

# 已克隆的项目初始化子模块
git submodule update --init --recursive

# 2. 安装 vcpkg（首次）
git clone https://github.com/microsoft/vcpkg.git <路径>
<路径>/bootstrap-vcpkg.bat                              # Windows
<路径>/bootstrap-vcpkg.sh                               # Linux
```

将 vcpkg 根目录配置为 `VCPKG_ROOT` 环境变量。CMake preset 会读取 `vcpkg.json`，在首次配置时自动安装依赖，无需单独执行 `vcpkg install`。Linux 还需要 Ninja 和 OpenGL/X11 等系统开发库，完整列表见 `.github/workflows/build.yml`。

> **需要完整子模块历史？** 如需调试某个 vendor 库的历史版本，运行：
> `git submodule foreach --recursive git fetch --unshallow`

#### Windows（VS 2022 Build Tools + vcpkg）

项目提供了 CMake Presets，按需选择：

```bash
# 默认（推荐）
cmake --preset default

# 可选 Vulkan 后端（需 Vulkan SDK 1.4+）
cmake --preset vs2022-vulkan

# 构建编辑器（其他目标：Engine、Server、Sandbox、EngineTests）
cmake --build build --config RelWithDebInfo --target Editor
```

#### Linux（Ninja + vcpkg）

```bash
# 配置（常用）
cmake --preset linux-default

# 可选：启用 ASAN/UBSAN 质量检查
cmake --preset linux-sanitize

# 构建
cmake --build build --target Editor
```

#### Run

```bash
# Windows
./build/Editor/RelWithDebInfo/Editor.exe

# Linux
./build/Editor/Editor
```

编辑器与服务器可从任意目录启动，会自动检测项目根目录。

### Vulkan 后端（可选）

项目提供 `vs2022-vulkan` CMake preset，启用 `ENGINE_ENABLE_VULKAN=ON`：
- 运行时通过 `Editor.exe --vulkan` 切换
- 不指定参数 → 继续使用 OpenGL 路径
- 详见 `docs/vulkan-migration/SPEC.md`

### CUDA 路径说明（可选）

CUDA 默认关闭，需安装 NVIDIA CUDA Toolkit，并使用以下 preset：

```bash
# 仅启用 CUDA，与默认构建共用 build/
cmake --preset vs2022-cuda
cmake --build build --config RelWithDebInfo --target Editor

# 本地论文实验：同时启用 Vulkan 与 CUDA，输出到独立目录
cmake --preset vs2022-benchmark
cmake --build build-benchmark --config RelWithDebInfo --target Editor
```

`vs2022-benchmark` 使用本机 Vulkan SDK 路径；在其他机器上使用前，应修改 `CMakePresets.json` 中的 `VULKAN_SDK` 或通过本地 preset 覆盖。

> **⚠️ 已知问题：CUDA 架构未显式指定。** 上述两个 preset 都没有设置 `CMAKE_CUDA_ARCHITECTURES`，实际按 `sm_75`（Turing）生成代码，而本机 RTX 3050 Ti 为 Ampere（CC 8.6）。功能上依靠 PTX JIT 可以运行，但会产生首次编译开销且无法使用 Ampere 专属优化，**性能基准数据会系统性偏低**。做性能实验前请显式指定架构：
>
> ```bash
> cmake --preset vs2022-cuda -DCMAKE_CUDA_ARCHITECTURES=86
> ```
>
> 详见 [#8 build(cuda): CUDA preset 未指定架构，实际按 sm_75 编译导致 Ampere 性能失真](https://github.com/LAJENUNESSE/Graduation_Project/issues/8)。

运行时日志若出现 `the provided PTX was compiled with an unsupported toolchain`，说明驱动版本过旧；此时引擎会自动回退到 OpenGL Compute 路径，更新驱动后可恢复 CUDA 路径。

> CUDA Toolkit 与驱动的版本对应关系见 [NVIDIA 官方文档](https://docs.nvidia.com/cuda/cuda-toolkit-release-notes/index.html#cuda-toolkit-major-component-versions)。

### 单元测试与网络同步

```bash
# Windows：构建并运行 GoogleTest 单元测试
cmake --build build --config RelWithDebInfo --target EngineTests
ctest --test-dir build -C RelWithDebInfo --output-on-failure

# Linux：多配置参数无需指定
cmake --build build --target EngineTests
ctest --test-dir build --output-on-failure
```

首次构建测试目标时，CMake 会通过 FetchContent 下载 GoogleTest。网络同步服务器可单独构建并启动：

```bash
# Windows
cmake --build build --config RelWithDebInfo --target Server
./build/Server/RelWithDebInfo/Server.exe --scene assets/scenes/network-demo.scene
```

随后在 Editor 菜单中选择“网络 → 连接服务器...”。协议冒烟测试、多终端演示及当前限制见 `docs/server/testing.md` 和 `docs/server/architecture.md`。

### Remotion 视频工程

`remotion/` 是独立的 React / Remotion / Three.js 工程，用于制作 SPH 原理与性能数据可视化视频，不参与 CMake 构建。需要 Node.js 20.19+，常用命令如下：

```powershell
cd remotion
npm install
npm run dev   # Studio 预览
npm run lint  # ESLint + TypeScript 检查
```

完整出片命令、镜头结构和渲染约定见 `remotion/README.md`。

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
| [Lua](https://www.lua.org/) | 脚本运行时（vcpkg） |
| [Asio](https://think-async.com/Asio/) | TCP 网络传输（vcpkg） |
| [NVIDIA CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit) | GPU 加速（可选） |

## Architecture

```text
Editor (exe) ── TCP ──► Server (headless exe)
      │                      │
      └──────────┬───────────┘
                 ▼
          Engine (静态库，由 Editor、Server、Sandbox 共享)
          ├─ Scene / Renderer / Reflection / Asset
          ├─ Physics / Audio / Media / Terrain
          ├─ Script / Network / Debug / ImGui
          └─ Platform: OpenGL 4.3 (默认) / Vulkan 1.2+ / CUDA Sidecar
```

Editor 内部由 `EditorLayer` 协调 SceneSession、Shell、Panel、Render、Viewport、Gizmo 等控制器及 UndoSystem。Server 与 Editor 复用同一 Engine，避免维护两份场景、物理和网络协议实现。

## Project Structure

```
Engine/                     核心引擎静态库
  src/
    Core/                   应用主循环、窗口、输入、日志、崩溃处理
    Renderer/               SceneRenderer 多 Pass 管线、PBR、IBL、后处理、粒子、流体
    Scene/                  ECS 场景图（façade + 服务化）、反射注册组件、运行时协调器
    Reflection/             编译期反射、ComponentRegistry、自动序列化/Inspector
    Asset/                  资产管理器（SlotMap + Handle）、异步加载、FileWatcher 热重载
    Physics/                Bullet3 物理世界、调试可视化
    Audio/                  OpenAL 音频引擎
    Media/                  FFmpeg 视频解码器
    Terrain/                高度图地形生成
    Script/                 NativeScript（C++）与 Lua 脚本系统
    Network/                TCP 协议编解码、客户端/服务端传输（Server 与 Editor 共享）
    Events/                 事件系统（发布-订阅）
    Debug/                  PerformanceMonitor、GPU Timer Query
    ImGui/                  ImGui 后端集成
  Platform/
    OpenGL/                 OpenGL 4.3 后端实现（默认）
    Vulkan/                 Vulkan 1.2+ 后端实现（可选，`--vulkan` 启用）
    CUDA/                   CUDA 加速管线（粒子 + SPH）、GL 互操作、错误回退

Editor/                     可视化编辑器
  src/
    EditorLayer             主协调器
    EditorSceneSession      场景生命周期（Edit ↔ Play）
    EditorShell             Dockspace + 快捷键
    EditorPanelCoordinator  面板聚合器
    EditorRenderController  SceneRenderer + 后处理
    EditorViewportController  EditorCamera + Framebuffer
    EditorSelectionGizmoController  3D 变换操纵器
    UndoSystem              撤销/重做（双栈命令历史）
    EditorBootstrapper      DI 工厂，创建并连线所有子系统
    EditorAssetDescriptor   资源类型 → UI 图标 + 拖拽映射
    VulkanSmokeLayer        Vulkan 烟雾测试层
    Panels/                 层级 / 属性 / 控制台 / 资源浏览器 / 渲染设置 / 脚本编辑器
    Scripts/                3 个示例脚本（PlayerController、RotateScript、CollisionTestScript）

Server/                     服务器权威的无头联机服务进程（60Hz 物理 + 20Hz 快照广播）

Sandbox/                    独立示例程序

tests/                      GoogleTest 单元测试（EngineTests）
  manual/                   网络冒烟与多终端演示脚本

assets/                     资源文件
  shaders/                  GLSL 着色器（PBR/IBL/后处理/粒子/SPH/SSFR/草地/阴影/地形）
  scenes/                   场景文件（YAML）
  models/                   3D 模型
  textures/                 纹理资源（含 Skybox cubemaps）
  audio/                    音频资源
  terrain/                  地形高度图

vendor/                     第三方依赖（Git 子模块）
remotion/                   Remotion 视频工程（SPH 原理讲解与数据可视化）
docs/                       技术调研报告、竞赛文档、论文、Vulkan 迁移、网络同步、性能分析
```

## CI/CD

GitHub Actions 自动化流程（`.github/workflows/build.yml`）：

- **Windows 构建**：`default` 预设构建 `Engine`、`Server` 与 `Editor`；另有 `vs2022-vulkan` 任务（自动安装 Vulkan SDK）
- **Linux 构建矩阵**：`linux-default` 与 `linux-sanitize`（ASAN/UBSAN），均构建 `Engine`、`Server` 与 `Editor`
- **测试执行**：在 `linux-sanitize` 任务中构建 `EngineTests` 并运行 CTest
- **构建加速**：ccache/sccache + vcpkg 缓存

`CMakePresets.json` 还保留了 `macos-default` 与 CLion 预设供本地使用，但当前 CI 不执行 macOS 构建，也没有 CodeQL 工作流。

## Platform Support

| 平台 | 状态 | 说明 |
|------|------|------|
| **Windows 10/11** (MSVC) | 主开发平台 | 完整功能，含可选 Vulkan/CUDA 后端 |
| **Ubuntu LTS** (GCC) | CI 验证 | 完整功能 |
| **其他 Linux 发行版** | 社区自行验证 | 推荐优先保证 Ubuntu LTS 兼容性 |
| **macOS** (AppleClang ARM64) | 仅本地编译验证 | macOS 仅支持 OpenGL 4.1，本引擎需要 4.3，`macos-default` 预设仅做编译检查 |

> **Linux 支持策略**：优先保证 Ubuntu LTS，其他发行版欢迎提 PR。
>
> **macOS 用户**：由于 Apple 已弃用 OpenGL 且最高仅支持 4.1，本引擎无法在 macOS 上运行。
> 如需体验，推荐使用 [CrossOver](https://www.codeweavers.com/crossover) 或 [Parallels Desktop](https://www.parallels.com/) 运行 Windows 版本。

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
