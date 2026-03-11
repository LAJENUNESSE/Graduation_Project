# Graduation Project — 3D Game Engine & Editor

湖北第二师范学院毕业设计：基于 C++ / OpenGL 的 3D 游戏引擎与可视化编辑器。

## Features

- **PBR 渲染管线**：OpenGL 4.3 Core Profile，基于物理的渲染（PBR），支持金属度/粗糙度工作流
- **光照与阴影**：方向光、点光源、聚光灯，方向光阴影映射（PCF 软阴影）
- **材质系统**：漫反射/法线/金属度/粗糙度/AO 纹理，纹理平铺控制
- **后处理**：HDR 色调映射、Bloom 辉光（半分辨率优化）、Gamma 校正、MSAA 抗锯齿
- **ECS 架构**：基于 EnTT 的实体-组件-系统，编译期反射 + 自动序列化/自动属性面板
- **GPU 粒子系统**：Compute Shader 驱动的粒子发射/模拟/渲染管线，支持 SPH 流体模拟（PCISPH）
- **GPU 草地渲染**：Compute Shader 在地形高度图上放置草叶，Billboard 渲染 + 风力动画
- **地形系统**：高度图驱动的地形生成，多层 Splatmap 纹理混合，多级 LOD
- **音频系统**：OpenAL 3D 空间化音频，支持 WAV 音频播放
- **视频流播放**：FFmpeg 解码 + RTSP/RTMP 网络流接入，后台异步连接，三重缓冲帧传递
- **物理系统**：Bullet3 刚体物理（动态/静态/运动学），碰撞检测（Box/Sphere）
- **天空盒**：六面 Cubemap 天空盒渲染
- **资产管理**：SlotMap 资源池 + Handle 安全引用，支持异步加载与热重载
- **可视化编辑器**：ImGui Docking + ImGuizmo 变换操纵器 + 场景层级面板 + 属性检查器
- **场景序列化**：YAML 格式保存/加载，反射驱动自动序列化
- **脚本系统**：NativeScript 原生 C++ 脚本绑定

## Build

```bash
# 初始化子模块
git submodule update --init --recursive

# 安装 vcpkg（首次）
git clone https://github.com/microsoft/vcpkg.git <路径>
<路径>/bootstrap-vcpkg.bat
<路径>/vcpkg install ffmpeg openal-soft --triplet x64-windows

# 构建
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<路径>/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config RelWithDebInfo --target Editor
```

## Run

```bash
./build/Editor/RelWithDebInfo/Editor.exe   # 可视化编辑器
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

## Project Structure

```
Engine/                 核心引擎静态库
  src/Core/             应用主循环、窗口、输入、日志
  src/Renderer/         渲染抽象层、SceneRenderer 多 Pass 管线
  src/Scene/            ECS 场景图、组件定义、系统（Systems/）
  src/Reflection/       编译期反射、自动序列化、自动属性面板
  src/Asset/            资产管理器（SlotMap + Handle）
  src/Audio/            OpenAL 音频引擎
  src/Media/            FFmpeg 解码器
  src/Physics/          物理系统（Bullet3 + 自定义求解器）
  Platform/OpenGL/      OpenGL 后端实现

Editor/                 可视化编辑器
  src/Panels/           UI 面板（层级、属性、视口）

assets/                 资源文件
  shaders/              GLSL 着色器（顶点/片段/计算 合并文件）
  scenes/               场景文件（YAML）
  textures/             纹理资源
  terrain/              地形高度图

vendor/                 第三方依赖（Git 子模块）
```

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
