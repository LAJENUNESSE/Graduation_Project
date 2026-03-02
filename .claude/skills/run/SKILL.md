---
name: run
description: 构建并运行 Editor
disable-model-invocation: true
allowed-tools: Bash
---

构建并运行编辑器。

## 执行步骤

1. 检查是否已有 build 目录配置，如果没有则先配置：
   ```bash
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
   ```

2. 构建 Editor：
   ```bash
   cmake --build build --target Editor
   ```

3. 如果构建成功，运行编辑器：
   ```bash
   ./build/Editor/RelWithDebInfo/Editor.exe
   ```

4. 如果构建失败，分析错误并给出修复建议，不要尝试运行。

## 注意

- Editor.exe 会自动检测项目根目录，可以从任何位置启动
- 运行时需要 OpenGL 4.3 支持的 GPU
