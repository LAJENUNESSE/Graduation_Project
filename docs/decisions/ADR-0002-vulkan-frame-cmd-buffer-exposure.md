# ADR-0002: Vulkan 主帧 command buffer 暴露策略

- **Status**: Accepted
- **Date**: 2026-05-17
- **Tags**: rhi, vulkan, frame-recording

## Context

Phase 7 把 IBL / Grass / SpatialHashGrid 这三个低频 compute 子系统迁完后，剩下粒子（每帧 4 dispatch）和流体（每帧 SPH 多 dispatch + PCISPH 1~8 迭代）。SPEC §3 D-3 已明确"每帧执行的 dispatch 禁止 `BeginSingleTimeCommands`"——SingleTime 内部 `vkQueueWaitIdle` 主线程 stall，每帧粒子提交一次就意味着每帧 stall。

但落地代码时撞上结构性裂缝：`VulkanContext::SwapBuffers()` 把整个主帧生命周期（`vkWaitForFences → Acquire → Begin cmd → Record clear/debug/ImGui → End → Submit → Present`）封死在一个函数内，主帧 command buffer `m_CommandBuffers[m_CurrentFrame]` 的 Begin/End 都在 SwapBuffers 内部，**外部没有"在 Begin 之后、End 之前"插入 compute dispatch 的入口**。

此外，Vulkan path 当前还走 `VulkanSmokeLayer`（P-4），Scene 渲染调用栈尚未接通，所以"粒子 Update 不会真正被调"——这意味着即使写完代码也只能编译验证，运行时验证要等 Phase 8。

需要在三条路线中选一条：

## Considered Options

### Option A：务实路线 — 粒子/流体也走 SingleTime（违反 D-3）

把 D-3 改造统一推迟到"Phase 7.5"，粒子/流体先用 `BeginSingleTimeCommands` + `vkQueueWaitIdle` 跑通。

- **优势**：与现状一致（IBL/Grass/Grid 都是 SingleTime），改动量最小，无需触碰 VulkanContext
- **代价**：每帧多次 `vkQueueWaitIdle`（粒子 1 次 + 流体 ≥ 1 次 + 粒子 SPH 时 PCISPH 8 迭代各 1 次 = 10+ stall/帧），等于"Phase 7 写完就要立即重做"
- **风险**：Phase 7.5 改造需要全部重写每帧调用方，本质上是 sunk cost

### Option B：激进路线 — 本次同步拆 VulkanContext

把 `SwapBuffers` 拆为 `BeginFrame() → (外部录制) → EndFrame()`，暴露 `GetCurrentFrameCommandBuffer()` 公共 getter。`SwapBuffers` 退化为 thin wrapper（内部清屏/Debug/ImGui pass 行为零变化）。粒子/流体每帧调用方直接录主帧 cmd。

- **优势**：一次到位，D-3 在 Phase 7 落实而不是甩到 Phase 7.5；后续粒子/流体迁移按模板执行
- **代价**：VulkanContext 接口扩面（公共 method 从 3 个增到 7 个），SwapBuffers 与新 API 双轨并存，要保证 SmokeLayer 行为零变化
- **风险**：拆出来的 BeginFrame 必须正确处理 swapchain recreate（返回 false）、EndFrame 信号化 readback fence 的时机

### Option C：混合路线 — AsyncReadback 主帧、其余 SingleTime

只让 AsyncReadback 把 `vkCmdCopyBuffer` 录到主帧 cmd（解决 D-4 ring 与 D-3 stall 联动问题），粒子的 dispatch 部分继续 SingleTime；PCISPH 60+ dispatch 仍 stall 等 Phase 7.5。

- **优势**：粒子 counter 回读最具代表性的"每帧异步"特性已主帧化
- **代价**：路径不统一（AsyncReadback 录主帧 cmd 但粒子 dispatch 走 SingleTime），调用方心智负担大
- **风险**：调用方需要在两种 cmd 录制模式间切换，bug 滋生面增加

## Decision

**选 Option B（激进路线）**，理由：

D-3 是 Phase 7 的硬约束，把它推迟到 7.5 等于把架构债压到下一阶段。VulkanContext 拆分本身工程量并不大（~80 行 + 一处 thin wrapper），且与 SmokeLayer 完全解耦——SmokeLayer 仍调 SwapBuffers，新拆出的 BeginFrame/EndFrame 是给后续高级用户用。后续 Scene 渲染（Phase 8）接通时也将直接消费这套接口，不会再次重构。

落地实现：
- `VulkanContext::BeginFrame()` 返回 `bool`，swapchain recreate 时返回 false 让调用方跳过本帧录制
- `VulkanContext::EndFrame()` 完成主 submit + 信号化 readback fence + present + frame advance
- `VulkanContext::RegisterReadbackFenceSignal(VkFence)` 让 AsyncReadback 把待信号化的 fence 入队，EndFrame 主 submit 之后追加零 cmd `vkQueueSubmit` 信号化（队列内顺序保证 cmdCopyBuffer 已完成）
- 主 submit 单 fence 限制（已被 `m_InFlightFences[m_CurrentFrame]` 占用）通过零 cmd submit 模式绕开

## Consequences

### Positive
- 粒子/流体迁移可以按"BeginFrame → 录 dispatch → EndFrame"模板执行，与 IBL/Grass 的 SingleTime 模板并存而非互斥
- AsyncReadback 3 槽 ring 的 fence 信号化得以统一在 EndFrame hook 中
- Phase 8 接通 Scene 渲染时直接消费这套接口，无需二次重构

### Negative
- VulkanContext 公共接口扩面：BeginFrame / EndFrame / GetCurrentFrameCommandBuffer / GetCurrentImageIndex / GetCurrentFrameIndex / RegisterReadbackFenceSignal / RecordImGuiPass（后者由 private 提升）
- SwapBuffers 与新 API "双轨并存"，调用方必须明确选择走哪条；混用（如先 BeginFrame 再 SwapBuffers）会触发 assert
- 主 submit 之后追加零 cmd submit 在 driver 端理论上有 < 0.01ms 开销，profile 上会看到额外 submit（可接受）

### Neutral
- SmokeLayer 行为完全不变（仍走 SwapBuffers 包装），但内部走的是新代码路径，回归风险需主线程验证
- SPEC §3 D-3 的"Phase 7.5 改造"条目可以视为已落实，D-10 / D-11 是其实现细节

## References

- Commit `777de43` — VulkanContext 拆 BeginFrame/EndFrame，暴露主帧 cmd buffer
- Commit `ad31c42` — VulkanAsyncReadback 3 槽 ring 消费 RegisterReadbackFenceSignal hook
- Commit `c7b2c92` — SpatialHashGrid::BuildVulkan(cmd, ...) 接受外部 cmd（粒子/流体的下游消费者）
- SPEC.md §3 D-3 / D-10 / D-11 / D-12
- SPEC.md §4 P-9 / P-10
- ADR-0001 — Shader 双路径 OpenGL / Vulkan（本 ADR 的前置约束）
