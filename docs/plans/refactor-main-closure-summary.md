# 当前 `main` 重构收口总结

## 目的

这份文档用于记录本轮主干重构已经完成的内容、已关闭的问题、仍然保留的工程债，以及下一阶段更合理的推进顺序。

它和以下计划文档的关系是：

1. [refactor-priority-overview.md](C:\Dev\Workspace\C++\Graduation_Project\docs\plans\refactor-priority-overview.md) 记录的是执行前的优先级判断
2. [refactor-p0-correctness.md](C:\Dev\Workspace\C++\Graduation_Project\docs\plans\refactor-p0-correctness.md) / [refactor-p1-lifecycle-and-concurrency.md](C:\Dev\Workspace\C++\Graduation_Project\docs\plans\refactor-p1-lifecycle-and-concurrency.md) / [refactor-p2-engineering-debt.md](C:\Dev\Workspace\C++\Graduation_Project\docs\plans\refactor-p2-engineering-debt.md) 记录的是分阶段计划
3. 本文档记录的是这些计划执行后的主干现状

当前主干头提交：`3c32b90`

---

## 一、本轮已经完成的主线工作

### 1. 编辑器主控拆分

`EditorLayer` 已从运行时行为上的 God Object 降级为编排器，核心职责已经拆入独立对象：

1. `EditorSceneSession`
2. `EditorPanelCoordinator`
3. `EditorShell`
4. `EditorViewportController`
5. `EditorSelectionGizmoController`
6. `EditorRenderController`

结果：

1. `Play / Stop`
2. `New / Open / Save`
3. viewport UI
4. gizmo / picking
5. panel render / context binding

已经不再集中堆在单个大函数中。

### 2. 组件责任边界统一

`Scene::Copy`、反射组件菜单、自定义组件 inspector 的职责边界已经统一。

结果：

1. 反射组件和手工组件不再双轨乱入
2. `AddComponent` 菜单和复制逻辑不再依赖脆弱的隐式约定

### 3. 路径系统脱离 `current_path()`

项目根路径和资源根路径已经改成显式解析，不再依赖启动目录玄学。

结果：

1. 编辑器入口
2. 场景序列化
3. 资源加载
4. 拖拽路径归一化

都已经转到项目根路径模型。

### 4. 关键功能正确性问题已处理

本轮已完成以下功能正确性修复：

1. 编辑器 picking 链路独立，不再混挂在 HDR/MSAA 结果上
2. 多选 gizmo 的撤销/重做恢复完整
3. `ConsolePanel` 改成日志快照读取，消除读写同一 `vector` 的并发风险
4. `EditorSceneSession` 改成编辑态 / 运行态双场景模型
5. 属性面板变换编辑已接入撤销重做
6. 删除实体后无效选中句柄不会再把属性面板打崩

### 5. 工程债第一轮已经明显收窄

本轮已经处理掉的工程债：

1. `PropertiesPanel` 大块 inspector 继续拆分，粒子发射器也已抽离
2. 窗口 / 图形上下文 / 应用入口 / 层栈的所有权模型已经统一到 `Scope`
3. `Engine` / `Editor` 的 target usage requirements 已收紧
4. `verify` 与 `release assert` 语义已经拆分
5. `CMakePresets.json` 已从本机硬编码路径迁到 `VCPKG_ROOT`

---

## 二、本轮新增并验证通过的收口项

这一轮收口最后新增、并已经过本机构建和手动验证的提交如下。

### 1. `922892d`

提交信息：

`重构：拆分命令历史的执行与记录语义`

落地内容：

1. `CommandHistory` 拆成
   - `ExecuteAndPushCommand`
   - `PushExecutedCommand`
2. gizmo 拖拽结束改用“已执行仅记录”的语义
3. 层级面板的创建 / 删除 / 改父子关系继续走“执行并入栈”
4. 选中无效实体后会及时清理，避免撤销删除后继续持有脏句柄

### 2. `783941e`

提交信息：

`重构：收紧属性面板组件菜单的 ImGui ID 边界`

落地内容：

1. 自定义组件和反射组件统一 `PushID`
2. 组件菜单统一使用 `+##ComponentMenu`
3. popup 统一使用 `ComponentSettings`
4. 属性面板的 `TransformComponent` 编辑接入撤销重做

### 3. `3c32b90`

提交信息：

`修复：为资源浏览器补齐文件系统错误兜底`

落地内容：

1. `AssetBrowserPanel` 改成 `error_code` 路径
2. 根目录缺失、当前目录失效、相对路径失败、目录遍历失败、文件大小失败都改成降级处理
3. 面板会显示最近一次文件系统错误，而不是直接抛异常把 UI 打崩

---

## 三、已经关闭的问题

截至当前主干，这些问题可以视为已关闭。

### 1. 命令历史接口语义混用

已通过 `ExecuteAndPushCommand / PushExecutedCommand` 拆开。

### 2. 属性面板组件菜单 ID 隐式冲突

已通过组件级 `PushID` 和统一 popup/button ID 收口。

### 3. 资源浏览器 throwing filesystem 路径

已改成非抛异常路径，并增加 UI 侧错误兜底。

### 4. 撤销删除实体后属性面板闪退

已通过无效选中句柄保护收口。

### 5. 多选 gizmo 撤销/重做不完整

已修复并验证通过。

### 6. `Play / Stop` 生命周期污染编辑场景

已切换到双场景模型，运行态副本在 `Stop` 时直接丢弃。

---

## 四、现在仍然存在但不再阻塞主线的问题

这些问题仍值得后续处理，但已经不属于“必须立即修”的收口项。

### 1. `EditorLayer` 编译期耦合仍偏宽

虽然运行时职责已经拆开，但 [EditorLayer.h](C:\Dev\Workspace\C++\Graduation_Project\Editor\src\EditorLayer.h) 仍直接持有较多具体类型。

这属于：

1. 编译期耦合面偏宽
2. 容易继续回灌 include 面
3. 不是当前行为正确性的阻塞项

### 2. `AssetBrowserPanel` 仍是同步目录浏览器

当前已经做到“坏环境不崩”，但还没做到：

1. 缓存目录状态
2. 异步枚举
3. 更细的错误分类和恢复策略

### 3. 构建系统仍缺规则层

虽然 preset、target usage requirements 和机器绑定已经明显改进，但还没建立：

1. 统一 warning policy
2. sanitizer 入口
3. test/lint 入口
4. 更正式的 CI 规则

### 4. 更多属性面板字段尚未接入撤销重做

当前重点先接通了：

1. gizmo 变换
2. 属性面板的 transform 变换

但并不是所有 inspector 字段都已经进入命令历史系统。

---

## 五、本轮手动验证结论

本轮主干已经完成并通过以下回归：

1. `New / Open / Save Scene`
2. `Play -> Stop` 连续多次切换
3. 中途切场景再 `Play -> Stop`
4. 单选 / 多选 gizmo 的 `Ctrl+Z / Ctrl+Y`
5. 属性面板 `位移 / 旋转 / 缩放` 的 `Ctrl+Z / Ctrl+Y`
6. 删除实体后撤销，不再闪退
7. viewport 点选、层级点选、`Q / W / E / R`
8. viewport resize
9. 模型 / 贴图 / 音频 / 视频 路径与拖拽
10. 资源浏览器在异常目录路径下不崩

结论：

当前主干已经通过本轮收口目标定义下的功能性回归。

---

## 六、建议的下一阶段顺序

如果继续往下做，不建议再立即开下一轮大拆。

更合理的顺序是：

### Step 1

先做 `EditorLayer` 的编译期瘦身。

目标：

1. 降 include 面
2. 降按值持有的具体类型数量
3. 让编排器继续保持“薄”

### Step 2

再补构建规则层。

目标：

1. warning policy
2. sanitizer 入口
3. test/lint 入口
4. 未来 CI 的最小骨架

### Step 3

最后再按需求决定是否扩展编辑器 UX：

1. 更多属性面板字段接入撤销重做
2. `AssetBrowserPanel` 缓存化
3. 选择模型和层级模型继续统一

---

## 七、一句话结论

到当前 `main` 为止，这轮重构已经完成了从“先止血”到“主干收口”的目标。

当前代码状态的判断是：

1. 关键 correctness 问题已处理
2. 高风险生命周期和并发问题已处理
3. 第一轮工程债已明显收窄
4. 后续工作应切换到“编译期瘦身和工程规则建设”，而不是继续大规模运行时重构
