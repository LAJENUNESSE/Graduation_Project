---
name: build
description: 构建项目（默认构建 Editor 目标）
metadata:
  short-description: 构建 CMake 目标
---

构建本项目的 CMake 目标。**Windows 为主流程**，Linux/VM 为次流程。

可选目标：`Editor`、`Engine`、`Sandbox`。省略 `--target` 则构建全部。

## Windows（VS 2022 Build Tools，主流程）

> cmake 不在全局 PATH 中，必须使用 VS Build Tools 内置的完整路径。
> 下方路径是唯一正确路径，直接复制使用。
>
> **禁止**使用 `find` / `ls` / `where` / `which` 等命令搜索 cmake 路径。
> **禁止**使用 `CMAKE="..." && "$CMAKE"` 变量模式（MINGW Bash 会把变量展开为空字符串，导致 `command not found`），必须直接写完整路径。

### 配置（二选一）

```bash
# 无 CUDA（default preset）
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset default

# 有 CUDA（vs2022-cuda preset，需安装 NVIDIA CUDA Toolkit）
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset vs2022-cuda
```

### 构建（Visual Studio 生成器必须指定 `--config`）

```bash
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target Editor
```

## Linux / VM（Ninja，次流程）

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target Editor
```

## 失败处理

1. 给出首个关键错误与定位文件。
2. 给出最小修复建议与下一次验证命令。
3. 如遇 submodule 缺失：
   ```bash
   git submodule update --init --recursive
   ```

## 注意事项

- 两个 preset 输出目录相同（`build/`），切换 preset 后需重新配置，但构建/运行路径不变。
- Windows/VS 生成器输出路径包含 `RelWithDebInfo/` 子目录；Linux/Ninja 不包含。
- 构建失败时不要继续执行依赖构建产物的步骤（如 `run`）。
