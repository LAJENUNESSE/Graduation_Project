# Graduation Project — 3D Game Engine & Editor

湖北第二师范学院毕业设计：基于 C++ / OpenGL 的 3D 游戏引擎与可视化编辑器。

## Features

- **渲染管线**：OpenGL 4.3 Core Profile，Phong/Blinn-Phong 多光源着色
- **光照系统**：方向光、点光源、聚光灯，方向光阴影映射（PCF 软阴影）
- **材质系统**：漫反射纹理、颜色叠加、纹理平铺、高光度控制
- **ECS 架构**：基于 EnTT 的实体-组件-系统
- **可视化编辑器**：ImGui Docking + ImGuizmo 变换操纵器 + ViewManipulate 方向指示器
- **场景序列化**：YAML 格式保存/加载，支持向后兼容

## Build

```bash
# 初始化子模块
git submodule update --init --recursive

# 构建
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
```

## Run

```bash
./build/Editor/Editor     # 可视化编辑器
./build/Sandbox/Sandbox   # 演示程序
```

## Dependencies

| Library | Purpose |
|---------|---------|
| [GLFW](https://github.com/glfw/glfw) | Windowing & input |
| [GLAD](https://gen.glad.sh/) | OpenGL function loader |
| [GLM](https://github.com/g-truc/glm) | Math |
| [EnTT](https://github.com/skypjack/entt) | Entity-Component-System |
| [spdlog](https://github.com/gabime/spdlog) | Logging |
| [Dear ImGui](https://github.com/ocornut/imgui) (docking) | Editor UI |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | 3D gizmos |
| [yaml-cpp](https://github.com/jbeder/yaml-cpp) | Scene serialization |
| [stb_image](https://github.com/nothings/stb) | Image loading |

## Project Structure

```
Engine/          Core engine static library
  src/Core/      Application loop, window, input, logging
  src/Renderer/  Rendering abstractions
  src/Scene/     ECS-based scene graph
  Platform/OpenGL/  OpenGL backend

Editor/          Visual editor application
  src/Panels/    UI panels (hierarchy, properties, viewport)

Sandbox/         Demo application
vendor/          Third-party dependencies
```

## License

This project is for educational purposes (graduation thesis).
