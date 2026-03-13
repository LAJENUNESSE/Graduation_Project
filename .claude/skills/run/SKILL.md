---
name: run
description: 构建并运行 Editor
disable-model-invocation: true
allowed-tools: Bash
---

构建并运行编辑器。

## 执行步骤

1. 检查当前平台，选择对应命令：

### Windows（VS 2022 Build Tools）

配置（如果没有 build 目录）：
```bash
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset default
```

构建：
```bash
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target Editor
```

运行：
```bash
./build/Editor/RelWithDebInfo/Editor.exe
```

> **禁止使用 `CMAKE="..." && "$CMAKE"` 变量模式**，必须直接写完整路径。

### Linux / VM（Ninja）

配置（如果没有 build 目录）：
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

构建：
```bash
cmake --build build --target Editor
```

运行：
```bash
./build/Editor/Editor.exe
```

2. 如果构建失败，分析错误并给出修复建议，不要尝试运行。

## 注意

- Editor.exe 会自动检测项目根目录，可以从任何位置启动
- 运行时需要 OpenGL 4.3 支持的 GPU
- Windows 输出路径含 `RelWithDebInfo/` 子目录，Linux/Ninja 不含
