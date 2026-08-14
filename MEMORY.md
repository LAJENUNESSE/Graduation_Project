# MEMORY.md — 跨任务长期教训

> 任务会结束，这里的教训不会。新增条目在此文件加索引行，正文留在条目各自的详细文件。
> 维护约定见 `.agents/skills/checkpoint`（三处分流：任务 SPEC.md / 全局 MEMORY.md / docs/decisions ADR）。

## 索引

- [DSH/Codex 沙箱降权导致外部工具崩溃](#沙箱降权导致-miktexvcpkggit-网络失败) — workspace-write 下 MiKTeX 引擎 fast-fail、vcpkg 配置失败、git 网络被拦，解法：对编译/网络类命令使用 danger-full-access

## 沙箱降权导致 MiKTeX/vcpkg/git 网络失败

> 首次记录：2026-08-14（论文重编译 + 主线合并 session）

| 编号 | 症状 | 触发条件 | 规避/根治 |
|------|------|---------|----------|
| P-1 | MiKTeX 所有引擎（xelatex/pdftex/luatex）连 hello-world 文档都崩溃，退出码 -1073740791（0xC0000409 fast-fail）；但 main.log 显示 "Output written ... 45 pages" 已写完 | 在 DSH 沙箱（workspace-write）下运行：进程被降权到 `LIU\CodexSandboxUsers`（只读），引擎收尾写 `%LOCALAPPDATA%\MiKTeX\miktex\log` 被拒 | 诊断：`initexmf --version` 报 `log4cxx: IO Exception : status code = 720005(拒绝访问)`；ACL 里有 CodexSandboxUsers 只读条目。**解法：编译命令带 danger-full-access**（一次性升级，用户批准后即可），论文四步链 xelatex→biber→xelatex→xelatex 可串成一条命令减少批准次数 |
| P-2 | cmake 配置报 "No CMAKE_CXX_COMPILER could be found" | 同上降权；真实根因是 vcpkg manifest 安装写 `C:\Dev\Tools\vcpkg\buildtrees` 被拒，报错在 vcpkg.cmake:953 | 看配置输出的最前面：`vcpkg install - failed` + `permission denied` 才是根因，编译器报错只是连锁。**解法同 P-1：danger-full-access** |
| P-3 | git fetch/push 报 `Bash/Service/CreateInstance/E_ACCESSDENIED` | workspace-write 沙箱拦截网络与凭据助手 | **解法同 P-1：danger-full-access**。另注意 escalation 后 pwsh 工具的命令会改由 bash 执行（`&` 调用运算符语法报错），此时改用 bash 工具 + escalation 最稳 |

### 该环境下的可用命令形态速查（2026-08 实测）

- **workspace-write（默认）**：pwsh 只读/工作区内写命令可用；一旦报 Bash/Service E_ACCESSDENIED，不要再重试，直接 escalation。
- **danger-full-access**：pwsh 会变 bash 包装（PowerShell 语法不可用），统一用 bash 工具。
- 论文编译、vcpkg 配置、git 远端操作、测试运行：全部需要 escalation。
