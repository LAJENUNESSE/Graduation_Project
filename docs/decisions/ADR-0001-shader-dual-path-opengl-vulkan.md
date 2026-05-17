# ADR-0001: Shader 单源双路径（OpenGL + Vulkan 共存）而非分叉重写

- **Status**: Accepted
- **Date**: 2026-05-16
- **Tags**: rhi, shader, vulkan-migration
- **Originating commits**: `6ca3cb3`, `97750ae`, `93b2e2d`

## Context

`feature/vulkan-backend` 分支引入 Vulkan 后端，需要让现有 36+ 个 `.glsl` shader 同时被 OpenGL 4.3 和 Vulkan 1.2+ 消费。两种 API 对 GLSL 的约束略有差异：

- Vulkan GLSL 要求显式 `layout(set=N, binding=M)`、push constant 用 `layout(push_constant)`、不允许散装 default uniform
- OpenGL 4.3 支持显式 binding，但散装 uniform 仍合法

迁移过程不能阻塞 main 分支的 OpenGL 功能（粒子/流体/草地/IBL/SPH 全部在用），且毕设演示主路径仍是 OpenGL。

## Considered Options

### Option A：分叉两套 shader（`*.gl.glsl` + `*.vk.glsl`）
- ✅ 各自最优写法，无 `#ifdef` 干扰
- ❌ 同一逻辑双份维护，bug 容易只修一边
- ❌ 文件数翻倍（36 → 72+），AssetBrowser / shader 热重载都要改
- ❌ 数学逻辑（如 SPH kernel）会重复

### Option B：单源 + `#ifdef VULKAN` 双路径 ✅ **选定**
- ✅ 一份逻辑、一处维护
- ✅ OpenGL 路径完全不动，零回归风险
- ✅ `VulkanShader.cpp` 在 shaderc 编译选项里显式注入 `VULKAN=1` macro，与 `GL_KHR_vulkan_glsl` 自动定义双保险
- ❌ shader 代码可读性下降（散布 `#ifdef`）
- ❌ 复杂 shader 双路径并存时容易遗漏一边

### Option C：删 OpenGL 路径，仅保留 Vulkan
- ✅ 维护成本最低
- ❌ **不可接受**：毕设主演示路径走 OpenGL，迁移分支必须不影响 main
- ❌ Vulkan 路径尚未达到功能对等（PBR pass / VulkanTextureCubemap / 主帧 ImGui 录制等都未完成）

## Decision

采用 **Option B**：所有迁移的 shader 用 `#ifdef VULKAN` 切换，OpenGL 分支完全保留不动。

约束：
- 写 shader 改动时必须同时验证两条路径
- 散装 uniform 在 Vulkan 分支改为 `layout(push_constant)`（小常量 ≤128 bytes）或 UBO（大常量、跨 stage）
- SSBO 在 Vulkan 分支显式 `layout(std430, set=0, binding=N)`

## Consequences

### Positive
- main 分支零回归（实测：`Editor.exe` 无 `--vulkan` 行为与 main 一致）
- 迁移可以渐进——单 shader 单独迁移不阻塞其他 shader
- IBL / Grass / SpatialHash 三块 compute 已按此模式落地，模板可复用

### Negative
- shader 阅读心智负担（如 `grass_placement.glsl` 现在有 `#ifdef VULKAN` 块）
- 编译验证需要双 preset 跑（`default` + `vs2022-vulkan`）
- 未来某天彻底删 OpenGL 时，需要一次性清理所有 `#ifdef`

### Neutral
- 新增 shader 必须照此模式（即使先只写 Vulkan，OpenGL 分支也要给个 fallback 或明确报错）
- `assets/shaders/` 目录命名约定保留（不改 `.vk.glsl` 后缀）

## References

- SPEC.md §3 D-2：[../vulkan-migration/SPEC.md](../vulkan-migration/SPEC.md)
- Commit `6ca3cb3` IBL shader 双路径示范
- Commit `97750ae` Grass shader uniform → push constant
- Commit `93b2e2d` Grid shader uniform → push constant
- `GL_KHR_vulkan_glsl` 扩展规范
