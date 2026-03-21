---
name: fixbugs
description: 批量验证并修复 bug 清单，完整执行 验证→修复→构建→提交 管线
allowed-tools: Read, Edit, Write, Grep, Glob, Bash, Agent
argument-hint: "提供 bug 清单（编号列表、文件路径或 docs/ 下的 bug 文档）"
---

批量修复 bug 管线。**必须实施实际代码修复，禁止止步于计划或分析文档。**

## 流程

### 1. 收集 Bug 清单

- 从用户消息、粘贴的列表或指定文件中提取 bug 条目
- 每个 bug 应包含：描述、相关文件/模块、严重程度（如有）

### 2. 逐一验证（先分诊后修复）

对每个 bug：
1. **读取相关源码**，确认 bug 是否真实存在
2. 如果 bug 不存在或已修复，标记为 **已排除** 并说明原因
3. 如果 bug 确认存在，标记为 **已确认** 并简要说明根因

输出一份简短的分诊摘要表（确认 / 排除），然后**立即开始修复**。

### 3. 分批实施修复

将已确认的 bug 按模块/文件分批：

对每批：
1. **实施代码修复**（Edit 工具）
2. **格式化修改的文件**：
   ```bash
   git diff --name-only --diff-filter=d HEAD | grep -E '\.(h|cpp)$' | grep -v '^vendor/' | xargs -r "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/bin/clang-format.exe" -i
   ```
3. **构建验证**：
   - Windows: `"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target Editor`
   - Linux: `cmake --build build --target Editor`
4. 如果构建失败，**立即修复编译错误**再继续
5. **提交**该批次：`git commit` 格式为 `fix(<模块>): <简述>`

### 4. 输出修复报告

全部完成后输出摘要：

| Bug | 状态 | 修复说明 | 提交 |
|-----|------|---------|------|
| #1 | 已修复 | ... | abc1234 |
| #2 | 已排除 | 非 bug，逻辑正确 | — |

## 注意

- **禁止止步于规划。** 分诊摘要产出后必须立即进入修复阶段
- 如果 bug 数量 > 10，使用 Agent 工具并行验证以加速分诊
- 每批最多 5 个修复，确保构建验证颗粒度合理
- 修复完成后如果有单元测试，运行测试确认无回归
