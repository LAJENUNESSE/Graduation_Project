Opencode — 仓库代理工作流与约束

简述
- 本文件定义 Opencode-agent 在本仓库工作的行为规范、工具调用规则、硬性禁止项与完成验证清单。所有 agent 输出与注释使用简体中文（除非另有书面约定）。

一、身份与意图门（Intent Gate）
- Agent 身份：Opencode-agent。默认把用户请求视为“需要执行”的意图，除非用户明确标注“仅解释/只读”。
- 在任何不确定情况下必须先探索仓库（EXPLORE），并在 1-4 次探索后仍不清楚时，向用户提单一精确问题以澄清（仅当探索无果时问）。

二、核心执行循环（必须遵守）
1) EXPLORE（并行）
   - 使用 explore/librarian 发起并行检索。每次检索记录并保留 task_id；发起后不得重复相同检索（Anti-Duplication Rule）。
2) PLAN
   - 列出将修改的文件、预期更改点、影响面、回退方案与验证步骤。对多步任务必须创建 todowrite 列表并逐步标记（in_progress / completed）。
3) DECIDE
   - 单文件小改（<10 行）由 agent 自行实现；跨模块或大改先生成计划并建议人工审查或使用 Agent Teams。
4) EXECUTE
   - 原子化改动、最小范围变更。不会在未授权下执行 git commit/push（见“提交策略”）。
5) VERIFY
   - 修改后运行 lsp_diagnostics（目标：0 error）、项目格式化（clang-format）、构建和相关测试。所有修改必须满足"通过验证"后方可视为完成。
6) CHECKPOINT（必要时）
   - phase 收尾或踩了非显而易见的坑后，调用 checkpoint skill 沉淀关键决策、技术选型舍弃理由、踩坑记录到对应位置（任务 SPEC.md / 全局 MEMORY.md / docs/decisions/ ADR）。
   - 不是每次 commit 都要做——只在有"非显而易见的选择"或"长期教训"时触发。

三、并行与委派规则
- 并行探索允许，但不可重复：一旦某信息委派给 explore/librarian，禁止本地再次做相同检索，等待并使用该背景任务结果。
- 背景任务必须保留 task_id；收集结果后逐一取消不再需要的背景任务（不要使用 background_cancel(all=true)）。

四、工具与技能调用规范
- 每次 task() / delegation 必须显式列出 load_skills（例如 build、fixbugs、git-master）。
- 危险操作（git push、修改 CI、替换凭证）需用户明确授权。
- 跨模块或架构性决策优先调用 metis/momus/oracle（如可用）进行前置评审。

五、提交与变更控制（硬规则）
- 未经用户明确授权：不得创建或推送任何 git commit。
- 禁止使用 as any / @ts-ignore / @ts-expect-error（类型安全抹除）。
- 禁止空的 catch(e) {}。
- 禁止删除或禁用测试以通过 CI。
- 提交前必须运行 clang-format（项目指定的 clang-format 路径），不要格式化 vendor/。

六、模块特定约定（路径限定规则）

- 项目 `.claude/rules/` 下有 11 个模块特定规则文件（renderer.md、scene-ecs.md、physics.md、shaders.md 等），每个文件通过 `paths:` frontmatter 限定作用域。
- OpenCode 不支持自动路径匹配注入，因此在操作对应模块时，agent 应主动查阅相关规则文件：
  | 操作路径 | 应查阅规则 |
  |---|---|
  | `Engine/src/Renderer/**` | `.claude/rules/renderer.md`、`.claude/rules/opengl.md` |
  | `Engine/src/Scene/**` | `.claude/rules/scene-ecs.md` |
  | `Engine/src/Physics/**` | `.claude/rules/physics.md` |
  | `Engine/src/Asset/**` | `.claude/rules/asset.md` |
  | `Engine/src/Reflection/**` | `.claude/rules/reflection.md` |
  | `Editor/**` | `.claude/rules/editor.md` |
  | `assets/shaders/**` | `.claude/rules/shaders.md` |
  | `tests/**` | `.claude/rules/tests.md` |
  | `Engine/Platform/Vulkan/**` | `.claude/rules/vulkan.md` |
- 规则优先级：AGENTS.md / OPENCODE.md > 模块规则 > 用户全局偏好。

七、权限与秘密处理
- 严格遵守 .claude/settings*.json 中的 allow/deny 列表（例如禁止读取 .env、*.pem、*.key）。
- 若发现明文凭证（如 settings.json 中的 ANTHROPIC_AUTH_TOKEN），立即报告并建议凭证轮换/移除（不得自动上传/共享）。

八、输出契约（Output Contract）
- 回应尽量简洁、使用简体中文。
- 代码变更必须包含：修改文件列表、关键代码片段摘要、验证步骤与验证结果（LSP/构建/测试日志）。
- 变更完成后说明下一步（人工审查/创建 PR/合并步骤）。

九、故障恢复
- 每次修改附带回退计划（如何重置改动或恢复到前一良好状态）。
- 若验证失败超过 3 次不同修复路径：停止更改并汇报完整诊断与建议（可请求 Oracle 协助）。

十、快速必须/禁止清单
- MUST：所有输出使用简体中文。
- MUST：遇到不确定信息时主动问用户。
- MUST：修改后运行 lsp_diagnostics（0 errors）+构建/测试通过或提供可重复日志与修复计划。
- MUST：phase 收尾或踩坑后用 `/checkpoint` skill 沉淀关键决策（参考 CLAUDE.md 第 7 步）。
- MUST NOT：未经授权创建/push commit。
- MUST NOT：暴露明文凭证；发现即上报。
- MUST NOT：使用 as any / @ts-ignore、空 catch、删除测试以通过 CI。

附录：常用命令（示例）
- Windows 构建（VS 2022 Build Tools）：
  "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset default
  "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target Editor

（来源：合并自仓库 AGENTS.md、CLAUDE.md 与 .claude/rules/*，并参考公开 AGENTS.md / CLAUDE.md 最佳实践样例）
