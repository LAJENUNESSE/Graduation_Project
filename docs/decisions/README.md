# 架构决策记录（ADR）

> **作用**：记录项目中**技术栈选型与重大架构决策**——尤其是"为什么不选 X / Y / Z"的对比理由。三个月后接续的人不必重新评估同样的选项。
> **触发**：通过 [`.claude/skills/checkpoint/SKILL.md`](../../.claude/skills/checkpoint/SKILL.md) 在 phase 收尾时沉淀。
> **格式**：参见 [MADR](https://adr.github.io/madr/) 简化版（Status / Context / Considered Options / Decision / Consequences / References）。

## 编号约定

- 4 位数字递增（ADR-0001, ADR-0002, ...）
- 文件名：`ADR-NNNN-kebab-case-title.md`
- 新建前先 `ls docs/decisions/` 找最大号 + 1
- 一旦分配，编号永不复用；废弃也保留

## Status 流转

```
Proposed ──→ Accepted ──→ Superseded by ADR-XXXX
                    └──→ Deprecated
```

- **Proposed**：方案落定但还未真正落地
- **Accepted**：已落地并在生产路径生效
- **Superseded**：被后续 ADR 取代，需在新 ADR 引用旧 ADR
- **Deprecated**：废弃但无替代

## 索引

| 编号 | 标题 | Status | Date | Tags |
|------|------|--------|------|------|
| [ADR-0001](ADR-0001-shader-dual-path-opengl-vulkan.md) | Shader 单源双路径（OpenGL + Vulkan 共存）而非分叉重写 | Accepted | 2026-05-16 | rhi, shader, vulkan-migration |

> 新增 ADR 后请在此表格追加一行，便于检索。
