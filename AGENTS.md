# Repository Agent Rules

本文件供 **Codex** 和 **Jules** 共同读取，定义项目级规范与各自职责。

## AI Workflow Roles

本仓库使用多 AI 工具协作开发，通过 Git（PR / Issue / Commit）进行交流。

| 工具 | 角色 | 指令文件 |
|------|------|---------|
| Claude Code | 主力开发 | `CLAUDE.md` |
| Codex | PR 代码审核 | `AGENTS.md`（本文件） |
| Jules | 异步任务（测试/文档） | `AGENTS.md`（本文件） |

### Pipeline Protocol

多 Agent **串行流水线**通过 `.ai/pipeline.json` 进行任务交接。每个 Agent 从前序阶段接手，完成后标记状态，推进流水线。

**pipeline.json 结构：**

```json
{
  "version": 1,
  "task": "任务标题",
  "description": "任务描述或 Issue 链接",
  "stages": [
    {
      "name": "阶段名",
      "agent": "claude-code | codex | jules",
      "status": "pending | in_progress | completed | needs_rework",
      "output": {
        "summary": "完成内容摘要",
        "files": ["涉及的文件列表"],
        "notes": "交接注意事项或审核反馈"
      }
    }
  ]
}
```

### Codex 专项指引

你的主要职责是**代码审核**。

1. 读取 `.ai/pipeline.json`，找到 `agent: "codex"` 的 stage
2. 确认前序阶段 `status` 为 `completed`
3. 将你的 `status` 设为 `in_progress`
4. 审核前序阶段 `output.files` 中列出的文件，关注：正确性、边界情况、性能隐患、架构一致性
5. **审核通过**：`status` 设为 `completed`，`output.notes` 写明结论
6. **需要返工**：将**前序阶段** `status` 改为 `needs_rework`，在你的 `output.notes` 写明具体问题

要批判性地审核，发现问题直接指出，不要轻易通过。

### Jules 专项指引

你的主要职责是**异步补充测试和文档**。

1. 读取 `.ai/pipeline.json`，找到 `agent: "jules"` 的 stage
2. 确认前序阶段 `status` 为 `completed`
3. 将你的 `status` 设为 `in_progress`
4. 根据前序阶段 `output`（改动摘要 + 文件列表）完成测试/文档编写
5. 完成后 `status` 设为 `completed`，填写 `output`

- 文档使用简体中文
- 提交前确保构建通过

## 1. 沟通与范围

- 默认使用简体中文进行说明、注释与变更说明。
- 本仓库是 C++17 + OpenGL 4.3 引擎/编辑器工程，仅在既有架构内修改，不引入无关新依赖。
- 不修改 `vendor/` 第三方源码，优先在项目层修复集成问题。
- **遇到不确定的信息时必须主动向用户提问，禁止猜测或脑补。** 宁可多问一句，也不要基于假设给出错误结论。
- 这包括但不限于：运行场景参数（粒子数量、迭代次数等）、用户意图、项目背景、复现步骤等。

## 2. 构建约定

- 首次拉取或三方缺失时先执行：`git submodule update --init --recursive`。
- 常用构建（Ninja）：
  - 配置：`cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`
  - 构建 Editor：`cmake --build build --target Editor`
- 已于 `2026-03-10` 验证当前 Windows 环境：Visual Studio Build Tools 2022 17.14、MSVC 14.44.35207、VS 自带 `CMake 3.31.6`、`Ninja 1.12.1`、`vcpkg` 工具链、`CUDA Toolkit 13.1`（`nvcc` 可用）。
- 当前普通 PowerShell 未将 `cmake`、`ninja`、`cl` 加入 `PATH`；命令行构建优先使用 Developer PowerShell，或显式调用 VS Build Tools 自带工具。
- 已验证现有 `build/` 目录可成功执行：`cmake --build build --target Editor --config RelWithDebInfo`。
- 仓库当前未在顶层 CMake 启用 `CUDA` 语言，也未发现 `.cu` / `.cuh` / `find_package(CUDAToolkit)` 现有接入；不要假定项目已默认支持 CUDA。
- Windows + MSVC 保持 `/utf-8` 生效（如 CMake 已统一处理，不重复改动）。
- `engpch.h` 由构建系统注入，除非必要不要手动补 include。

## 3. 架构边界

- `Ref<T>` = `std::shared_ptr<T>`，`Scope<T>` = `std::unique_ptr<T>`（见 `Core/Base.h`）。
- 平台相关实现只放在 `Engine/Platform/`；`Engine/src/` 通过抽象接口访问。
- 如新增 CUDA 相关实现，统一放在 `Engine/Platform/CUDA/` 或同级平台目录；`Engine/src/` 不直接依赖 `cuda_runtime.h`。
- 资源路径按项目根目录解析（如 `assets/shaders/PBR.glsl`）。

## 4. Scene / ECS / Reflection

- 新组件必须完成反射声明与注册，否则不会出现在编辑器和序列化中：
  - 声明：`ENGINE_COMPONENT`、`ENGINE_PROPERTY`
  - 注册：`REGISTER_COMPONENT_BEGIN/END`、`REGISTER_PROPERTY`
- 组件结构体必须可默认构造、可拷贝。
- `PropertyType` 必须与字段真实类型匹配；`Transient` 属性不序列化。
- `Scene::DestroyEntity()` 前先清理对应 Bullet 刚体。

## 5. Renderer / OpenGL / Shader

- Shader 文件使用单文件 `#type` 分段，且 `#type` 必须在 `#version` 之前。
- 渲染 Shader 使用 GLSL 330；Compute Shader 使用 430+。
- 当前 GPU 计算主路径仍是 OpenGL Compute Shader；只有在性能或能力明确不足时再引入 CUDA 分支，避免无必要地长期维护两套实现。
- 如后续引入 CUDA，优先以独立 target 方式接入 `CUDAToolkit`；仅在确需编译 `.cu` 时启用 CUDA 语言，不要把 CUDA 编译/链接选项扩散到全部目标。
- 如涉及 CUDA / OpenGL 互操作，必须明确资源所有权、映射/解绑顺序与同步点，避免跨 API 读写竞争。
- 顶点布局约定：`location 0/1/2/3 = position/normal/texcoord/tangent`。
- Uniform 名称与 C++ 设置端保持完全一致（区分大小写）。
- Compute 流程保证：
  - `local_size` 与 dispatch 参数匹配
  - SSBO `std430` 与 C++ 结构体对齐
  - 跨 pass 读写有 `memoryBarrier`/`barrier` 同步
- `Material::Set()` 仅缓存值，需通过 `Bind()` 触发实际上传。

## 6. Asset / Physics / Editor

- `AssetManager::Init()` 后才能加载资源；每帧调用 `AssetManager::Update()`。
- 异步加载当前仅支持纹理，Mesh 默认同步加载。
- 每个 Scene 只能选择一种物理后端（Bullet 或自研），禁止混用。
- Editor 删除实体采用延迟删除，避免迭代器/句柄失效。
- Inspector 旋转以“度”显示，内部按“弧度”存储。
- 编辑器 UI 文本默认中文；资源路径必须位于项目目录内（避免 `..` 越界）。

## 7. 提交前检查

- 至少构建受影响目标（最低 `Editor`），确认无新增编译/链接错误。
- 涉及 Shader、反射组件、资源加载、物理同步时，至少走通一次对应运行路径。
