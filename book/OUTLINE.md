# 《从一个窗口到一个引擎》全书大纲

> 本大纲由三路调研综合产出：git 历史考古（490 提交全量分析）、docs/ 文档资产
> 盘点、源码架构普查。每章标注：**时间线**（构建历程定位）、**代码锚点**
> （当前真实文件）、**素材来源**（可挖掘的既有文档/提交证据）。
>
> 章节状态：✅ 样章/初稿完成 · 📝 占位卡（含素材索引，待 M3 扩写）

## 第 I 部分 地基

### 第 1 章 绪论：这本书讲什么、怎么读 📝
- 项目全景（TikZ 架构总览图）：490 commits / ~45,600 行（Engine 25.2k +
  Platform 11k + Editor 8.8k）/ 38 shaders / 11 scenes
- 五段式章节结构说明；代码锚点阅读法；环境搭建（CMake preset + vcpkg + 子模块）
- 素材：git 月度提交分布（2026-02 → 2026-08）；README

### 第 2 章 引擎骨架：Application 与主循环 ✅（样章）
- 时间线：2026-02-21 首个提交即含 Application 骨架
- 锚点：`Core/Application.h/.cpp`、`Core/LayerStack.h`
- 内容：游戏循环三形态对比；单例与 CreateApplication 控制反转；
  帧边界显式化（BeginRenderFrame/EndRenderFrame no-op 虚函数，D-16 伏笔）；
  关闭拦截器；混合睡眠限帧（timeBeginPeriod + _mm_pause）
- 踩坑：Windows.h NOMINMAX（8d67964）；float 计时精度（80fccb2）；
  析构顺序空指针（2c5ad95）；spin-wait 56%→0%（9eb9a79）

## 第 II 部分 看见画面

### 第 3 章 渲染抽象层：在 OpenGL 之上筑墙 📝
- 时间线：Phase R1 重构（5043e83）→ Vulkan 迁移 Phase 1 抽象泄漏归零（27398fc）
- 锚点：`RendererAPI.h`、`RenderCommand.h`（静态门面+统计内联）、
  Buffer/Shader/Texture/Framebuffer/VertexArray 工厂族、`Platform/OpenGL/`（22 文件）
- 素材：renderer.md 规则卡（8-Pass 管线全景）；Material dirty-flag；
  BindTextureView(void*) 防 vulkan.h 泄漏设计；RenderQueue 按 Shader 排序

### 第 4 章 场景与 ECS：EnTT 与 façade 架构 📝
- 时间线：Phase 2 引入 EnTT（2026-02-21）→ Phase 1 RuntimeStore（4dab189）
  → Phase 2 façade 收束（a6676de）→ Phase 3 组件元数据统一（83dcf39）
- 锚点：`Scene/Scene.h`（六成员 façade）、`Components.h`（20+ POD）、
  `Entity.h`、HierarchyService/WorldTransformService、`Systems/` 八系统、
  `Runtime/SceneRuntimeCoordinator`（Play/Stop 双场景快照）
- 踩坑：DestroyEntity O(N²)（641f8ae）；重复 UUID 拒绝；EnTT size_hint 兼容

### 第 5 章 编译期反射：一套宏驱动的编辑器数据流 📝
- 锚点：`Reflection/ComponentMeta.h`（ENGINE_COMPONENT 宏家族逐层解剖）、
  `ComponentRegistry.cpp`、`AutoInspector.cpp`、`AutoSerializer.cpp`
- 核心：offsetof 属性注册；static bool lambda 类型擦除七操作
  （Has/Add/Get/Remove/Copy/Snapshot/Restore）；注册即自动获得 UI+序列化
- 对比素材：docs/ue-reflection-research.md

### 第 6 章 资产管理：SlotMap、异步加载与热重载 📝
- 锚点：`Asset/SlotMap.h`、`AssetHandle.h`、`AssetManager.cpp`、
  AsyncLoadQueue、FileWatcher
- 素材：3955e2e 完整实现；freelist 污染修复（d05ea6b）；句柄 Type 字段根治
  冲撞断言（182f5e7）；线程安全 LoadAsync 去重（576116a/6ef37c9）

## 第 III 部分 让世界可信

### 第 7 章 物理系统：从手写冲量到 Bullet3 📝
- 叙事弧：Phase 9 手写冲量（7341800）→ 审计穿模 10 bug（89fe5b4）→
  移除自研 PhysicsWorld 强制 Bullet（9cff9ce）——"自己写一遍才懂轮子"
- 锚点：`Physics/BulletPhysicsWorld.cpp`、`SDFMath.h`（header-only 可测）
- 碰撞数学案例群：OBB SAT（1e3a12e）、退化轴文档、惯性张量 mat3+
  平行轴修正（b85497d）、陀螺力矩（ecf3017）、接触面计算

### 第 8 章 SPH 流体与 GPU 粒子：Compute 管线三部曲 📝
- 时间线：Phase 10a-f（29d41cc 起）→ PCISPH（58a69bf）→ SSFR（e46f088）
- 锚点：ParticleSystemGPU（Compute 4-Pass + Indirect Draw）、FluidSystemGPU
  （WCSPH/PCISPH）、SpatialHashGrid（hash/prefix_sum/scatter）、
  SPHKernelMath.h、GPUAsyncReadback、14 个 sph/fluid/grid shader
- 学术素材：thesis/chapter4-sph.tex 全文复用；Solenthaler 2009 δ 推导
  （00146b7）；Akinci 2013 表面张力（6cf61fb）
- 踩坑富矿：SSBO slot 覆盖致流体不可见（e63d819）；GL_BLEND 泄漏
  （b85f9d5）；28ms 同步回读 stall（01de4e3）；CUDA/GLSL 一致性三连修
  （dc72895）；emit 后 compact 差一帧（547e3ad）

### 第 9 章 CUDA sidecar：引入、移除与重引入 📝
- 三幕剧叙事（技术栈取舍史最佳案例）：引入（7290534，四部曲调研文档）→
  移除（e71ea47/a18fd0c，23 文件 4566 行）→ 重引入（a1f9def wip 系列）
- 锚点：`Platform/CUDA/` 11 文件：CudaGLInteropContext、CudaParticlePipeline、
  CudaSPHPipeline、CudaPoisonState.h（atomic 中毒标志 + 永久回退，
  有单元测试）、Pimpl 隐藏依赖（6969f20/708eba8）、数据布局静态断言（1fd159d）

### 第 10 章 音视频与地形脚本：外围模块速写 📝
- OpenAL（Phase A，8eca5f6）；FFmpeg RTSP/RTMP（ef60961）；地形高度图+
  SplatMap（488d6fe，2026-02-26）；NativeScript + Lua 双后端（b36119b 起，
  含热重载 5427f8d）；ScriptEditorPanel + 补全方案 B 设计文档

## 第 IV 部分 编辑器与工程化

### 第 11 章 编辑器架构：EditorLayer 的膨胀与瘦身 📝
- 叙事线：2026-03 连环 refactor（十余 commit）将上帝类逐块拆出：
  RenderSettingsPanel → SceneSession（双场景模型 b8bceb1）→ Shell（4ebfdfe）
  → ViewportController（2a03f2b）→ GizmoController（135c8c6）→
  PanelCoordinator（7da22f4）→ 收口总结 cd766a4
- 锚点：EditorLayer.h/.cpp + 七控制器/面板；docs/plans/refactor-*×5

### 第 12 章 Undo/Redo：命令模式与反射快照 📝
- 锚点：UndoSystem.h/.cpp + CommandHistory.h/.cpp（执行/记录语义分离，
  14 测试用例）
- 机制：命令模式 × 反射 Snapshot/Restore（std::any 组件快照）
- 踩坑：Redo 保留原始 UUID（fd1eaee）；删除递归保存子树（27f62c4）；
  Play 模式暂停记录（2371ecf）；多选变换（62479f9）

### 第 13 章 性能工程：三次真实调优案例 📝
- 案例一：VTune Hotspots 定位鼠标卡顿 → GetFileAttributesExW 文件系统热点
  （perf/perf-analysis-mouse-lag.md，前后数据齐全）
- 案例二：NSight + CSV 交叉分析 50k 粒子 SPH 帧时间，VSync 量化阶梯根因
  （perf_investigation_round3.md）
- 案例三：uarch 探索 Retiring 13.8% / Back-End 47% + CUDA 同步自旋
  （vtune-optimization-guide.md）
- 工具链锚点：PerformanceMonitor、ProfileTimer、GPUTimerQuery（GL+CUDA 双路径）

### 第 14 章 Vulkan 后端迁移：一次受控的双后端改造 📝（全书最重章）
- 骨架 = SPEC.md Phase 1-8：抽象泄漏归零 → 基础设施清屏 → RendererAPI 核心
  → shaderc 运行时编译 → VMA 资源族 → ImGui 多视口 → Compute 全家桶迁移
  （IBL/草地/网格/粒子/SPH）→ PBR 主路径接通（DrawIndexed 进行中）
- 锚点：`Platform/Vulkan/` 40 文件 ~6000 行
- 决策库：SPEC §3 十六条 D-x（背景+结论+影响范围格式现成）；
  ADR-0001（shader 单源双路径三方案对比）、ADR-0002（主帧 cmd 暴露策略）
- 踩坑录：SPEC §4 十七条 P-x 直接成节

## 附录

### 附录 A 三后端流体基准实验协议 📝
- 协议（fluid-compute-benchmark-spec.md）+ 正式数据（experiment-data-summary.md）
  + ~30 个 benchmark 工程 commit（37daaef..3cfee23：确定性初始状态、等步数
  一致性校验、密度误差容差校准、断点续跑、实验矩阵自动化）
- 可复用图表：docs/thesis/figures/generated/ 17 张 PDF/PNG

---

## 写作纪律（每章通用）

1. **现状以当前代码为准**——Obsidian 笔记只取决策背景，不描述现状
2. **每个论断给锚点或提交号**——可回溯是本书与普通教程的核心差异
3. 样章（第 2 章）为详略基准：~200 行 TeX，4-6 个代码片段，踩坑 2-4 条
4. 图表优先复用 thesis/figures/generated/，新绘一律 TikZ
