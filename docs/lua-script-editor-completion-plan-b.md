# Lua 脚本编辑器 IDE 代码提示（方案 B）设计文档

## 1. 背景与目标

当前 Lua 脚本编辑器位于 `Editor/src/Panels/ScriptEditorPanel.cpp`，编辑控件使用 `vendor/ImGuiColorTextEdit/TextEditor`，已具备：

- Lua 语法高亮（`TextEditor::LanguageDefinition::Lua()`）
- 多文档 Tab 管理
- 文件打开/保存/另存为

本方案目标是在现有架构下实现“接近 IDE 体验”的代码提示（自动弹出 + 键盘选择 + 上下文成员提示），并保持实现复杂度可控。

## 2. 范围定义

### 2.1 本方案包含

- 自动触发补全（输入 `.` / `:` / 标识符后触发）
- 手动触发补全（`Ctrl+Space`）
- 候选列表 popup（支持上下键、回车、Tab、Esc）
- 基于上下文的成员补全：
  - `Engine.` -> Engine API
  - `self.Entity:` -> Entity 方法
  - `Key.` / `Mouse.` -> 常量
- 前缀替换插入（只替换当前 token，不破坏前后文本）

### 2.2 本方案不包含

- LSP 集成（`lua-language-server`）
- 全语义类型推断（跨文件符号解析）
- 重构级能力（重命名、跳转定义）

## 3. 当前约束与关键问题

基于现状代码，主要约束如下：

1. `TextEditor` 无公开“字符输入回调/按键拦截回调”。
2. `TextEditor` 未暴露光标屏幕坐标，popup 难以稳定贴靠 caret。
3. popup 打开时，`TextEditor` 默认会先消费方向键、Enter、Tab，导致候选导航冲突。

因此，方案 B 的核心是：**对 `vendor/ImGuiColorTextEdit` 做最小、可控的扩展**。

## 4. 总体架构

```text
ScriptEditorPanel
  ├─ LuaCompletionCatalog（静态词库）
  ├─ LuaCompletionProvider（过滤/排序/上下文判定）
  └─ CompletionSession（每个文档一份运行态会话）

TextEditor（vendor 扩展）
  ├─ OnCharTyped 回调
  ├─ OnKeyPressed 回调（可消费）
  └─ GetCursorScreenPos()
```

设计原则：

- 词库与 UI 解耦（Provider 可单测）
- 会话按文档隔离（多 Tab 不串状态）
- vendor 改动聚焦“输入事件 + 光标坐标”，不侵入着色/渲染主逻辑

## 5. 数据结构设计

以下为建议结构（头文件草案级别）：

```cpp
enum class CompletionKind
{
    Keyword,
    Function,
    Method,
    Constant,
    Snippet,
};

struct CompletionItem
{
    std::string    Label;        // 显示文本，例如 "GetTranslation"
    std::string    InsertText;   // 实际插入文本，例如 "GetTranslation()"
    std::string    Detail;       // 右侧说明，例如 "Entity method"
    CompletionKind Kind;
    int            Priority = 0; // 基础优先级（越大越靠前）
};

enum class CompletionContext
{
    Global,
    EngineMember,
    EntityMember,
    KeyMember,
    MouseMember,
};

struct CompletionQuery
{
    CompletionContext      Context = CompletionContext::Global;
    std::string            Prefix;       // 当前 token 前缀
    TextEditor::Coordinates ReplaceFrom; // 替换起点
    TextEditor::Coordinates ReplaceTo;   // 替换终点（通常是光标）
};

struct CompletionSession
{
    bool                              Visible = false;
    int                               SelectedIndex = 0;
    std::vector<const CompletionItem*> Candidates;
    CompletionQuery                   ActiveQuery;
};

struct LuaCompletionCatalog
{
    std::vector<CompletionItem> GlobalItems;
    std::vector<CompletionItem> EngineItems;
    std::vector<CompletionItem> EntityItems;
    std::vector<CompletionItem> KeyItems;
    std::vector<CompletionItem> MouseItems;
};
```

### 5.1 词库来源

- Lua 关键字/内置函数：沿用 `TextEditor::LanguageDefinition::Lua()` 语义
- 引擎 API：以 `assets/scripts/API.md` 为来源，先以 C++ 静态表维护（MVP）
- 后续可选：增加脚本从 `API.md` 生成 C++ 表，避免文档与词库漂移

## 6. 触发流程设计

### 6.1 自动触发

触发条件：

- 输入 `.`、`:`
- 输入字母/数字/下划线并且当前已有可识别前缀

流程：

1. `TextEditor` 在字符写入后回调 `OnCharTyped(ch)`。
2. `ScriptEditorPanel` 读取当前光标与行文本，构建 `CompletionQuery`：
   - `Engine.` -> `EngineMember`
   - `self.Entity:` -> `EntityMember`
   - `Key.` -> `KeyMember`
   - `Mouse.` -> `MouseMember`
   - 其他 -> `Global`
3. `LuaCompletionProvider` 返回候选并排序。
4. 若候选非空，`CompletionSession.Visible = true`。

### 6.2 手动触发

触发条件：`Ctrl+Space`

流程与自动触发一致，但忽略“是否触发字符”的限制，强制弹出当前上下文候选。

### 6.3 候选导航与确认

popup 可见时，按键规则：

- `Up/Down`：移动选中项
- `Enter/Tab`：确认插入
- `Esc`：关闭 popup

通过 `TextEditor` 新增按键回调完成“先询问外部是否消费”，消费后不执行编辑器默认行为。

## 7. UI 草图（ImGui）

```text
┌────────────────────────── 脚本编辑器 ──────────────────────────┐
│ ...                                                           │
│ self.Entity:GetTra|                                           │
│               ┌──────────────────────────────┐                │
│               │ > GetTranslation()    Method │                │
│               │   GetWorldTranslation() Method│               │
│               │   SetTranslation(x,y,z) Method│               │
│               └──────────────────────────────┘                │
│ ...                                                           │
└───────────────────────────────────────────────────────────────┘
```

UI 行为要求：

- popup 锚点在光标下方，超出窗口时自动向上弹
- 最多显示 `N=8` 条，超出滚动
- 选中项显示 `Label + Kind + Detail`
- 鼠标 hover 同步选中，单击确认

## 8. Vendor 改造点（`TextEditor`）

目标：最小改动支持外部补全系统。

### 8.1 头文件新增 API（建议）

文件：`vendor/ImGuiColorTextEdit/TextEditor.h`

```cpp
using CharTypedCallback = std::function<void(ImWchar)>;
using KeyPressedCallback = std::function<bool(ImGuiKey key, bool ctrl, bool shift, bool alt)>;

void SetCharTypedCallback(CharTypedCallback cb);
void SetKeyPressedCallback(KeyPressedCallback cb);

ImVec2 GetCursorScreenPos() const;
bool   IsFocused() const;
```

### 8.2 源文件改造点（建议）

文件：`vendor/ImGuiColorTextEdit/TextEditor.cpp`

- 在字符真正插入后触发 `m_CharTypedCallback`
- 在 `HandleKeyboardInputs()` 的默认分支前询问 `m_KeyPressedCallback` 是否消费
- 在 `Render()` 内缓存 caret 屏幕坐标（供 popup 锚点使用）

### 8.3 兼容性要求

- 回调默认空，不影响现有行为
- 若外部不注册回调，行为与当前版本一致
- 不修改现有快捷键语义（仅允许外部“优先消费”）

## 9. 插入与替换策略

确认候选后：

1. 使用 `SetSelection(ReplaceFrom, ReplaceTo)` 选中前缀
2. 调用 `Delete()` 删除前缀
3. 调用 `InsertText(item.InsertText)` 插入候选
4. 关闭 popup

这样可保证：

- 仅替换当前 token
- Undo/Redo 逻辑继续由 `TextEditor` 托管

## 10. 过滤与排序策略

建议评分函数（由高到低）：

1. `Label` 前缀精确匹配（区分大小写）
2. 前缀匹配（不区分大小写）
3. 子串匹配
4. `Priority` 高者优先
5. `Label` 字典序兜底

首版不做复杂 fuzzy（如编辑距离），确保性能稳定与行为可预测。

## 11. 性能策略

- 每次触发只过滤当前上下文集合（避免全量扫描）
- 候选数量上限（例如 200）
- popup 不可见时不做过滤
- 输入连续触发时复用上一轮 query 的前缀结果（可选优化）

## 12. 测试与验收

### 12.1 手工测试矩阵

1. `Engine.` 自动弹出 Engine 成员
2. `self.Entity:` 自动弹出 Entity 方法
3. `Ctrl+Space` 在任意位置可手动弹出
4. `Up/Down/Enter/Tab/Esc` 行为正确，不干扰编辑
5. 多 Tab 场景下候选状态隔离
6. 光标在窗口边缘时 popup 位置不出界

### 12.2 建议单元测试（可选）

- Provider 的上下文判定测试
- 评分排序测试
- 前缀替换范围计算测试

## 13. 风险与回退

风险：

- vendor 改造可能与未来上游同步冲突
- 键盘事件消费顺序处理不当会破坏编辑体验
- 光标坐标计算在滚动/缩放字体下可能偏移

回退策略：

1. 用宏开关禁用自动补全，仅保留原编辑器行为
2. 保留 `Ctrl+Space` 的手动触发作为降级方案
3. 将 vendor 改造集中在单独 commit，必要时可快速回滚

## 14. 实施里程碑（建议）

### M1：基础能力（1-2 天）

- 完成 `TextEditor` 回调与光标坐标 API
- 跑通 `Ctrl+Space` + 静态候选 popup

### M2：自动触发与上下文（1-2 天）

- `.` / `:` 自动触发
- `Engine.` / `self.Entity:` / `Key.` / `Mouse.` 上下文

### M3：交互打磨（0.5-1 天）

- 键盘/鼠标细节
- popup 定位、边缘翻转、滚动体验

## 15. 结论

方案 B 可行，技术风险主要集中在 `TextEditor` 的最小改造。若按本文档推进，建议先完成 M1 验证“事件回调 + popup 定位”这两个关键点，再决定是否继续全量实现。
