---
name: run
description: 构建并运行 Editor
disable-model-invocation: true
allowed-tools: Bash
---

构建并运行编辑器。**Windows 为主流程**，Linux/VM 为次流程。

> 构建部分的详细禁令与 preset 说明见 `build` skill。本 skill 专注 Editor 构建 + 运行链路。

## 执行步骤

1. 检查当前平台，选择对应命令。
2. 构建失败时分析错误并给出修复建议，**不要**尝试运行。

## Windows（VS 2022 Build Tools，主流程）

> cmake 不在全局 PATH 中，使用 VS Build Tools 内置路径。
> **禁止**使用 `find` / `ls` / `where` / `which` 搜索 cmake 路径。
> **禁止**使用 `CMAKE="..." && "$CMAKE"` 变量模式，必须直接写完整路径。

配置（如果没有 `build/` 目录；二选一）：

```bash
# 无 CUDA
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset default

# 有 CUDA（需 NVIDIA CUDA Toolkit）
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset vs2022-cuda
```

构建：
```bash
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target Editor
```

运行：
```bash
./build/Editor/RelWithDebInfo/Editor.exe
```

## Linux / VM（Ninja，次流程）

```bash
# 配置（如 build/ 未生成）
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
# 构建
cmake --build build --target Editor
# 运行
./build/Editor/Editor.exe
```

## 注意

- Editor.exe 会自动检测项目根目录，可从任何位置启动。
- 运行时需要 OpenGL 4.3 支持的 GPU。
- 使用 `vs2022-cuda` preset 构建时，粒子/SPH 走 CUDA 计算路径；CUDA 失败会全局中毒并回退到 OpenGL Compute。
- Windows 输出路径含 `RelWithDebInfo/` 子目录，Linux/Ninja 不含。
