# 代码简化审计 · 总览（00-overview）

> 审计日期：2026-08-31 · 分支：`feature/vulkan-drawindexed`（含工作区未提交改动）
> 范围：第一方代码约 4.6 万行 —— `Engine/src`（23.6k）、`Engine/Platform`（12.8k）、`Editor/src`（9.5k）、`benchmark/` 脚本
> **本文档只做记录，不附带任何代码修改；后续实施按批次进行，每条实施前需重新核对该条所述位置。**

## 一、审计方法与可信度

- 派出 2 个只读审计子代理并行扫描（一个覆盖 `Engine/src`，一个覆盖 `Engine/Platform` + `Editor/src` + `benchmark/`）。
- **所有"死代码"结论均要求 grep 证据**（排除 `vendor/`、`build/`）。主代理对高风险条目逐条人工复核了调用点。
- 已剔除 1 条子代理误报：`FluidSystemGPU::EmitVulkan` 并非死代码——`FluidSystemGPU.cpp:471` 在 `#ifdef ENGINE_ENABLE_VULKAN` 分支内正常调用（子代理 grep 时漏掉了条件编译分支）。教训：**死代码判定必须覆盖 `#ifdef` 分支与宏注册路径（反射、事件、模板实例化）**。

## 二、总统计

| 区域 | 发现条数 | 预计可简化行数 | 其中低风险部分 |
|---|---|---|---|
| Engine/src（[01-engine-core.md](01-engine-core.md)） | 26 | 900–1100 | ~450 |
| Engine/Platform（[02-platform-backends.md](02-platform-backends.md)） | 11 | 600–800 | ~180 |
| Editor/src + benchmark（[03-editor-benchmark.md](03-editor-benchmark.md)） | 10 | 550–800 | ~180 |
| **合计** | **47** | **约 2000–2700（占第一方代码 4–6%）** | **低风险约 800** |

> 行数为估算值（删除行 + 合并后净减行），按当前工作区行号口径；实际收益以实施时为准。

## 三、实施批次建议（按风险/收益比排序）

### P0 · 零风险死代码删除（预计 ~450 行，可一次实施）
纯删除、grep 已验证无调用点、无行为变化：
- 01 文档条目 B1–B9（LoadAsync/AsyncLoadQueue 异步链、ShaderLibrary、SceneRenderer::Render 等）
- 02 文档条目 V4（Vulkan 访问器/成员死代码批）
- 03 文档条目 E5（不可达分支）、E7（转发包装）

**验证方式：编译（default 与 vs2022-vulkan 双 preset）+ 启动 Editor 打开示例场景冒烟。**

### P1 · 低风险机械合并（预计 ~600 行，每条独立提交）
重复代码收敛，行为逐字段一致：
- 01 条目 A1–A6、A8–A10（Lua 分发五连抄、SDF 调试体三连抄、StorageBuffer/Texture 工厂样板、全屏四边形三连抄等）
- 02 条目 V2、V3、V7、V8（Vulkan Draw 三份填充、barrier 重复、附件创建、IBL Shutdown）

**验证方式：双 preset 编译 + 对应功能路径人工冒烟（粒子发射、SPH 流体、后处理 BLOOM、IBL 生成）。**

### P2 · 中风险重构（需针对性回归，单独立分支逐条做）
- 01 条目 A7（跨文件 Vulkan pipeline 构建收敛）、A11/A12、F1（SceneSerializer 键表收敛，**场景文件格式不能变**）
- 02 条目 V1（VulkanTexture 双类合并）、V5（调试日志删除）、V6（staging 收敛，**触及上传热路径**）、O1（IBL 生成合并，**需对比一次生成结果**）、O3（legacy 路径，**取决于 Sandbox 去留**）
- 03 条目 E1（资产路径输入块 ×8，UI 需目检）、E2（Vec3 控件合并，undo 语义）、E3（ConsolePanel 高亮裁剪，**用户可见**）、E4、E6、E8–E10（benchmark 脚本，**基准战役已结案，动前确认不再重采**）

### P3 · 待用户拍板后实施
- 03 条目 E3 的裁剪档位（a/b/c 三选一）
- 03 条目 E8（`benchmark/tmp_analysis.py` 删除或归档）
- 02 条目 O3（OpenGL legacy 单附件路径去留）
- 01 条目 B8（`IsRawMouseInputEnabled` 是否保留接口完整性）

## 四、禁止触碰清单（已确认不值得动）

| 区域 | 理由 |
|---|---|
| `Engine/Platform/CUDA/CudaSPHPipeline.cu`、`CudaParticlePipeline.cu` 的 kernel 近重复（~150-200 行潜力） | 论文核心性能数据来源；合并改变寄存器占用与数值行为，答辩前禁止触碰 |
| `VulkanSceneDrawDispatcher::PackAndUploadGlobals`（451-647） | 与 GLSL std140 布局逐字节对齐的**契约代码**，static_assert 已锁布局；压缩收益低、断链风险高 |
| `VulkanContext::BeginFrame/EndFrame` 的 fence/semaphore 时序（193-282） | 实测修出来的时序设计（host-acquire-fence + swapchain semaphore 复用规避），重写等于重踩坑 |
| OpenGL 与 Vulkan 各自的 IBL/Framebuffer 整体实现 | 跨后端独立是架构设计，不引入抽象基类"统一" |
| `SpatialHashGrid.cpp` 三 pass 前缀和、`SPHCommon.cpp` 手写几何算法 | 结构清晰、算法选择合理 |
| `Reflection/ComponentRegistry.cpp` 宏注册样板 | 反射框架固有成本，宏已是最优表达 |
| `FluidBenchmarkLayer.cpp` 状态机 | 刚产出定稿基准数据的链路，不宜动 |

## 五、全局风险提示

1. **工作区在途改动**：`FluidSystemGPU.cpp/.h`、`SceneRenderer.cpp/.h` 存在未提交修改。本文档涉及这两文件的行号基于**当前工作区状态**；实施前先提交或暂存（stash）在途工作，避免行号漂移与冲突。
2. **双后端编译**：所有 C++ 改动必须同时过 `--preset default`（纯 OpenGL）与 `--preset vs2022-vulkan` 两个 preset 的编译；条目涉及 `#ifdef ENGINE_ENABLE_VULKAN` 时尤其注意非 Vulkan 编译单元不要引用 Vulkan 头。
3. **反射与序列化**：删除组件成员前确认 `ComponentRegistry.cpp` 注册表与 `SceneSerializer.cpp` 序列化路径不引用（文档已逐条核对，实施时再复核一遍）。
4. **基准工具链**：`benchmark/` 下脚本是论文结案工具链（D-24/D-25 决策），改动默认值前先在 SPEC 记录口径变化。
