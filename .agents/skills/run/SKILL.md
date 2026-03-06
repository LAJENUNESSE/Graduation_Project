---
name: run
description: 构建并运行 Editor
metadata:
  short-description: 构建并启动编辑器
---

构建并运行编辑器可执行文件。

## 执行步骤

1. 若 `build/` 未配置，先执行：
   ```bash
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
   ```

2. 构建 Editor：
   ```bash
   cmake --build build --target Editor
   ```

3. 构建成功后运行：
   ```bash
   ./build/Editor/RelWithDebInfo/Editor.exe
   ```

4. 若构建失败：
   - 返回关键错误与定位，不执行运行步骤。

## 注意事项

- 运行需要 OpenGL 4.3 能力。
- Editor 启动时会自动定位项目根目录。
