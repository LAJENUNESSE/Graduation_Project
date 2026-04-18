---
name: build
description: 构建项目（默认构建 Editor 目标）
disable-model-invocation: true
allowed-tools: Bash
argument-hint: "[target]  可选: Editor, Engine, Sandbox, 留空=全部构建"
---

构建本项目。**Windows 为主流程**，Linux/VM 为次流程。

## 执行步骤

1. 检查当前平台，选择对应的构建命令。
2. 根据参数构建：
   - 提供 target 参数（如 `/build Editor`）时，传给 `--target`
   - 无参数时省略 `--target`，构建全部
3. 构建失败时分析错误输出并给出修复建议，**不要**继续执行依赖构建产物的操作。

## Windows（VS 2022 Build Tools，主流程）

> cmake 不在全局 PATH 中，必须使用 VS Build Tools 内置的完整路径。
> 下方路径是唯一正确路径，直接复制使用。
>
> **禁止**使用 `find` / `ls` / `where` / `which` 等命令搜索 cmake 路径。
> **禁止**使用 `CMAKE="..." && "$CMAKE"` 变量模式（MINGW Bash 会把变量展开为空字符串，导致 `command not found`），必须直接写完整路径。

配置（二选一）：

```bash
# 无 CUDA（default preset）
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset default

# 有 CUDA（vs2022-cuda preset，需安装 NVIDIA CUDA Toolkit）
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset vs2022-cuda
```

构建（必须指定 `--config`）：

```bash
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target <target>
```

## Linux / VM（Ninja，次流程）

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target <target>
```

## 注意

- Windows 使用 VS 生成器，构建时必须指定 `--config RelWithDebInfo`
- Linux 使用 Ninja 生成器，构建类型在配置时指定
- 两个 preset 输出目录相同（`build/`），切换 preset 后需重新配置，但构建/运行路径不变
- Windows/VS 生成器输出路径包含 `RelWithDebInfo/` 子目录；Linux/Ninja 不包含
- 如果遇到 submodule 问题，提示用户运行 `git submodule update --init --recursive`
