---
name: checkpoint
description: 在子任务/phase 完成后，把"关键决策、踩坑、技术栈舍弃与选择"沉淀到对应位置（任务 SPEC.md / 全局 MEMORY.md / docs/decisions ADR）。Use proactively when the user invokes /checkpoint, says "整理一下"、"沉淀一下"、"phase 收尾"、"做个 checkpoint"、"任务先告一段落"。
---

# /checkpoint — 任务沉淀工作流

> **存在意义**：commit message 记录"做了什么"，但记不下"为什么这么选"、"踩了什么坑"、"放弃了哪条路"。这三类信息一旦丢失，三个月后接续的人会重蹈覆辙。本 skill 把它们抽取出来分流落盘。

## 什么时候触发

**该触发**
- 完成一个 phase / 子任务 / 一组相关 commit
- 做了一次技术栈选型（如选 Vulkan 不选 D3D12、选 VMA 不选裸 vkAllocateMemory）
- 踩了一个非显而易见的坑并修好了
- session 即将结束，本轮有值得长期记住的东西

**不要触发**
- 单次 bug 修复且根因显然（git log + commit message 已经够）
- 仅调参数（数值不是 Decision）
- "完成了 phase X 第 N 项" 这种进度更新——直接改 SPEC.md 的 checkbox 即可

## 三处分流

| 内容类型 | 落盘位置 | 判断标准 |
|---|---|---|
| 当前任务的决策 / 踩坑 / 下一步 | **任务专属 SPEC.md** | 三个月后接续该任务的人查得到才有用 |
| 跨任务、跨项目长期成立的教训 | **全局 `MEMORY.md`** | 别的任务/分支也可能踩到；不会因为这个任务关闭就过期 |
| 技术栈舍弃与选择 / 重大架构决策 | **`docs/decisions/ADR-NNNN-*.md`** | 含"为什么不用 X / Y / Z"的对比决策，需要被独立检索 |

**分流速记**：
- 任务死了就过期 → SPEC.md
- 任务死了仍有效 → MEMORY.md
- 含 "选 A 不选 B C D" → ADR

## 执行步骤

1. **盘点本轮变更范围**
   - 默认起点：对应 SPEC.md §1 "最近 commit" 之后
   - 或用户指定 commit/分支
   - `git log --oneline <start>..HEAD`

2. **逐 commit 审问三个问题**
   - 这次有没有非显而易见的选择？为什么这样选而不那样？
   - 有没有踩到非预期的坑？怎么绕开/根治的？
   - 有没有放弃某条技术路径？放弃的理由是什么？

3. **按速记分流落盘**（用下方模板）

4. **必要时更新 SPEC.md §1**
   - "最近 commit" 改成本轮最新
   - "下一步"如有调整同步

5. **更新 MEMORY.md 索引行**
   - 新增条目要在 MEMORY.md 加一行 `- [Title](file.md) — one-line hook`
   - 不要把全文写进 MEMORY.md，索引只放 hook

6. **输出报告**
   - 本轮新增的 Decision 编号、Pitfall 编号、ADR 编号
   - 修改了哪些文件
   - 没找到值得沉淀的（坦诚说"本轮没东西要沉淀"，强行编没有意义）

## 模板

### Decision（追加到 SPEC.md §3 Decision Log）

```markdown
### D-N：<一句话标题>
> **Decision**：<具体选了什么>
> **Why**：<不得不选的背景；最好附数据/链接/commit hash>
> **How to apply**：<新代码/新人遇到类似场景时怎么用；什么时候这条规则失效>
```

### Pitfall（追加到 SPEC.md §4 或 MEMORY.md）

```markdown
| P-N | <症状> | <什么时候会触发> | <怎么规避或根治> |
```

### ADR（新建 `docs/decisions/ADR-NNNN-<short-title>.md`）

```markdown
# ADR-NNNN: <选型主题>

- **Status**: Proposed | Accepted | Superseded by ADR-XXXX | Deprecated
- **Date**: YYYY-MM-DD
- **Tags**: <如 rhi, build, asset, scene>

## Context
<为什么需要做这个选择？问题是什么？>

## Considered Options
- **Option A：xxx** — 优势 / 代价 / 风险
- **Option B：yyy** — 优势 / 代价 / 风险
- **Option C：zzz** — 优势 / 代价 / 风险

## Decision
<选了哪个 + 一句话总结理由>

## Consequences
- **Positive**：<选了之后带来什么收益>
- **Negative**：<引入了什么新约束/技术债>
- **Neutral**：<额外要做的事>

## References
- 相关 commit: `<hash>`
- 相关 PR / Issue
- 调研文档：`docs/xxx-research.md`
```

## 编号约定

- Decision：在任务 SPEC.md 内部递增（D-1, D-2, ...）
- Pitfall：同上（P-1, P-2, ...）
- ADR：**全局**递增 4 位编号（ADR-0001, ADR-0002, ...），新建前先 `ls docs/decisions/` 找最大号 + 1
- ADR 文件名：`ADR-NNNN-kebab-case-title.md`，小写英文连字符

## 反模式（不要做）

- ❌ 把 commit message 改写一遍当 Decision——空话
- ❌ 把"今天搞定了 X"写成 ADR——这是进度不是决策
- ❌ 强行凑数——没有值得沉淀的就直接说"本轮没东西要沉淀"
- ❌ 在 MEMORY.md 索引行写完整内容——索引行 ≤150 字符
- ❌ ADR Status 写完就不动了——后续被替代时要回来改成 `Superseded by ADR-XXXX`
