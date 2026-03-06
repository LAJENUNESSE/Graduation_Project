---
name: build
description: 构建项目（默认全部构建，支持指定 CMake target）
metadata:
  short-description: 构建 CMake 目标
---

构建本项目。

## 执行步骤

1. 若 `build/` 未配置，先执行：
   ```bash
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
   ```

2. 按参数构建：
   - 指定目标（如 `Editor` / `Engine` / `Sandbox`）：
     ```bash
     cmake --build build --target <target>
     ```
   - 未指定目标时构建全部：
     ```bash
     cmake --build build
     ```

3. 若失败：
   - 先给出首个关键错误与定位文件。
   - 再给出最小修复建议与下一次验证命令。

## 注意事项

- 默认使用 Ninja + `RelWithDebInfo`。
- 如遇 submodule 缺失，提示执行：
  ```bash
  git submodule update --init --recursive
  ```
