---
name: run
description: 构建并运行 Editor
metadata:
  short-description: 构建并启动编辑器
---

构建并运行编辑器。**Windows 为主流程**，Linux/VM 为次流程。

> 构建部分的详细禁令与 preset 说明见 `build` skill。本 skill 专注 Editor 构建 + 运行链路。

## Windows（VS 2022 Build Tools，主流程）

### 配置（如 `build/` 未生成）

```bash
# 无 CUDA
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset default

# 有 CUDA（需 NVIDIA CUDA Toolkit）
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset vs2022-cuda
```

### 构建 Editor

```bash
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target Editor
```

### 运行

```bash
./build/Editor/RelWithDebInfo/Editor.exe
```

## Linux / VM（Ninja，次流程）

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target Editor
./build/Editor/Editor.exe
```

## 失败处理

- 构建失败时，给出关键错误并**停止**，不尝试运行。
- 如遇 submodule 缺失：`git submodule update --init --recursive`

## 注意事项

- Editor 启动时会自动检测项目根目录，可从任意目录运行。
- 运行时需要 OpenGL 4.3 能力的 GPU。
- 使用 `vs2022-cuda` preset 构建时，粒子/SPH 会走 CUDA 计算路径；CUDA 失败会全局中毒并回退到 OpenGL Compute。
- **禁止**使用 `find` / `ls` / `where` / `which` 等命令搜索 cmake 路径。
- **禁止**使用 `CMAKE="..." && "$CMAKE"` 变量模式，必须直接写完整路径。
