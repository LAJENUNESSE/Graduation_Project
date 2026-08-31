# Editor/src + benchmark 简化审计（03-editor-benchmark）

> 行号基于 2026-08-31 工作区状态。实施前先重新定位。
> Editor 是答辩演示的可见面，涉及 UI 的条目（E1/E2/E3）实施后必须过一遍面板目检。
> 风险标记：🟢 低 / 🟡 中 / 🔴 高（不建议动）

---

# E. Editor/src（约 9500 行）

## E1 🟡 PropertiesPanelCustomDrawers.cpp —— 资产路径输入块复制 6+ 次

- **位置**：`Editor/src/Panels/PropertiesPanelCustomDrawers.cpp:251-303`（diffuse）、`309-363`（normal）、`371-393`/`414-434`（heightmap/splatmap）、`444-475`（layer 反照率/法线）、`514-523`（grass）、`736-774`（audio）、`875-916`（lua）
- **类别**：冗余代码
- **问题**：「256 字节 buf + memset/strncpy + InputText(EnterReturnsTrue) + (Load/Clear 按钮) + DragDropTarget」模式重复 8 次，每处 30-50 行。已亲读 248-310 确认 diffuse 块结构。
- **简化方案**：匿名命名空间提取统一字段绘制函数（重构后完整代码骨架）：

```cpp
namespace
{
    // 通用"资产路径字段"：手输 + 拖拽 + 浏览 + 可选加载/清除按钮
    // 返回 true 表示 handle 被修改（调用方据此置 modified）
    bool DrawAssetHandleField(AssetType type, const char* label, const std::string& currentPath,
                              AssetHandle& handle, const char* fileFilter, const char* fileDesc,
                              bool withBrowse, bool withLoadClear, const char* loadLabel, const char* clearLabel)
    {
        bool modified = false;

        char pathBuf[256];
        memset(pathBuf, 0, sizeof(pathBuf));
        std::strncpy(pathBuf, currentPath.c_str(), sizeof(pathBuf) - 1);
        if (ImGui::InputText(label, pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            std::string newPath(pathBuf);
            handle = newPath.empty() ? AssetHandle{} : AssetManager::Load(type, newPath);
            modified = true;
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(GetEditorAssetDescriptor(type).PayloadType))
            {
                handle = AssetManager::Load(type, std::string(static_cast<const char*>(payload->Data)));
                modified = true;
            }
            ImGui::EndDragDropTarget();
        }

        if (withBrowse && ImGui::Button("浏览..."))
        {
            std::string relStr;
            if (TrySelectProjectAssetPath(fileFilter, fileDesc, "资产", relStr))
            {
                handle = AssetManager::Load(type, relStr);
                modified = true;
            }
        }

        if (withLoadClear)
        {
            ImGui::SameLine();
            if (ImGui::Button(loadLabel) && !currentPath.empty())
            {
                handle = AssetManager::Load(type, currentPath);
                modified = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(clearLabel))
            {
                handle = {};
                modified = true;
            }
        }
        return modified;
    }
} // namespace

// 调用点（diffuse 例）：
if (DrawAssetHandleField(AssetType::Texture2D, "纹理路径",
                         AssetManager::GetPath<Texture2D>(component.DiffuseTextureAsset),
                         component.DiffuseTextureAsset, "*.png;*.jpg;*.jpeg;*.bmp;*.tga", "图片文件",
                         true, true, "加载纹理", "清除纹理"))
    modified = true;
```

> ⚠️ 实施核对点：各调用点的按钮组合、layout 差异（`ImGui::SameLine()` 位置）、`TrySelectProjectAssetPath` 的过滤器/描述字符串逐个对齐；有差异的用参数表达，差异过大的（如 layer 材质的双字段联动）保留手写。文件 966 行预计缩至 ~800。
- **预计收益**：约 150-180 行
- **风险**：🟡 中——UI 逐面板目检（材质、地形、草、音频、Lua 脚本组件各打开一次）；拖拽 payload 类型不能串。

## E2 🟡 PropertiesPanel.cpp 与 CustomDrawers 各持一份 DrawVec3Control

- **位置**：`Editor/src/Panels/PropertiesPanel.cpp:151-244`（带 EditStarted/Finished 的 undo 版）vs `Editor/src/Panels/PropertiesPanelCustomDrawers.cpp:106-157`（精简版）
- **类别**：冗余代码
- **问题**：同一 XYZ 控件两份实现；CustomDrawers 版无法支撑 undo 是因为少返回激活状态。
- **简化方案**：把 PropertiesPanel 版移入共享工具（如 `Editor/src/Panels/DrawerUtility.{h,cpp}`），编辑状态改为可选出参：

```cpp
struct Vec3ControlEditState
{
    bool  EditStarted  = false;
    bool  EditFinished = false;
    glm::vec3 StartValue{};
};
// editState 传 nullptr 即"无 undo 语义"（CustomDrawers 现状）
bool DrawVec3Control(const char* label, glm::vec3& value, float speed, float resetValue,
                     Vec3ControlEditState* editState = nullptr);
```

- **预计收益**：约 55 行
- **风险**：🟡 中——undo 语义只在 PropertiesPanel 路径生效是**现状行为**，合并后必须保证 CustomDrawers 传 nullptr 时 undo 记录不产生（否则 UndoSystem 会出现幽灵快照）。实施后测一次 transform 拖拽 undo + 材质参数拖拽。

## E3 🟡 ConsolePanel.cpp —— 单函数约 480 行的日志语法高亮器（过度设计）

- **位置**：`Editor/src/Panels/ConsolePanel.cpp:176-581`
- **类别**：过度设计 / 嵌套层级
- **问题**：为控制台逐 token 上色实现了小型词法分析器（数字/十六进制/指数/路径/布尔/诊断词/类型后缀/标点切分，15 个嵌套 lambda），`OnImGuiRender` 单函数约 575 行。
- **简化方案（三档任选，属用户可见变化，P3 拍板）**：
  - **(a) 最小档**：整行按日志级别单色 + 引号内字符串高亮，删除全部 token 分类（约 -250 行）。演示效果损失：数字/路径不再单独着色。
  - **(b) 中间档**：保留 keyword/number/path 三类 token，删除标点切分与两字符运算符合并（约 -150 行）。
  - **(c) 零行为档**：全部 lambda 移入匿名命名空间具名函数，行数不变，可读性提升；顺带把 `IsNumberToken`（55 行）拆分。
- **预计收益**：a/b 档 150-250 行；c 档 0 行
- **风险**：🟡 中——控制台是演示时翻错误信息的界面，裁剪前先确认答辩演示中是否依赖 token 高亮定位问题。

## E4 🟢 EditorLayer.cpp —— 保存三胞胎

- **位置**：`Editor/src/EditorLayer.cpp:221-244`（SaveSceneQuick）、`246-263`（SaveSceneAs）、`427-448`（PerformAutosave）；CheckAndPromptRestore 内还有半份
- **类别**：冗余代码
- **问题**：`GetSceneForSaving → SaveSceneToPath(CollectRenderSettings) → ClearDirty → m_AutosaveTimer=0 → CleanupAutosave → UpdateWindowTitle` 五步在 3 处重复。已亲读 221-263 确认。
- **简化方案**（重构后完整代码）：

```cpp
// EditorLayer.h 私有段：
bool SaveSceneToPath(const std::string& path, bool setCurrentPath);

// EditorLayer.cpp：
bool EditorLayer::SaveSceneToPath(const std::string& path, bool setCurrentPath)
{
    auto& session = m_Boot->SceneSession();
    Ref<Scene> sceneToSave = session.GetSceneForSaving(m_ActiveScene);
    bool ok = session.SaveSceneToPath(sceneToSave, path,
                                      m_Boot->RenderController().CollectRenderSettings(sceneToSave));
    if (ok)
    {
        if (setCurrentPath)
            session.SetCurrentScenePath(path);
        session.ClearDirty();
        m_AutosaveTimer = 0.0f;
        CleanupAutosave();
        UpdateWindowTitle();
    }
    return ok;
}

bool EditorLayer::SaveSceneQuick()
{
    auto& session = m_Boot->SceneSession();
    if (!session.IsDirty())
        return true;
    if (!session.HasScenePath())
    {
        SaveSceneAs();
        return !session.IsDirty();
    }
    return SaveSceneToPath(session.GetCurrentScenePath(), false);
}

void EditorLayer::SaveSceneAs()
{
    std::string filepath = FileDialogs::SaveFile("*.scene", "\xe5\x9c\xba\xe6\x99\xaf\xe6\x96\x87\xe4\xbb\xb6");
    if (filepath.empty())
        return;
    SaveSceneToPath(filepath, true);
}
// PerformAutosave 与 CheckAndPromptRestore 内的保存段同理收敛
```

- **预计收益**：约 28 行 + 行为一致性自动获得（三处保存后处理本就有漏 `UpdateWindowTitle` 之类的隐患）
- **风险**：🟢 低。注意 Quick 原实现无 `SetCurrentScenePath`，As 原实现无 `m_AutosaveTimer=0` 顺序差异——统一后以"成功才清脏"语义为准（与原三处一致）。**PerformAutosave（427-448）实施前需亲读对齐，其原版可能不弹窗口、失败静默，注意保留静默语义。**

## E5 🟢 AssetBrowserPanel.cpp —— EnsureCurrentDirectoryValid 不可达分支

- **位置**：`Editor/src/Panels/AssetBrowserPanel.cpp:96-134`
- **类别**：控制流（不可达分支）
- **问题**：链条 `if(ec)…else if(有效)…else if(!ec&&!exists)…else if(ec)…else…` 中第 4 个 `else if (ec)`（113-115）不可达（ec 在前面已为 false 才会走到那里），且 114-115 与 102-104 完全重复。
- **简化方案**：改卫语句式：

```cpp
std::error_code ec;
if (fs::equivalent(... ec) 或首个判定)
{
    /* ec 分支：回退提示 + 返回 */
    return ...;
}
if (is_directory(...))
    return true;
/* 其余：回退提示 */
```

- **预计收益**：约 10 行 + 可读性
- **风险**：🟢 低（不可达分支删除无行为变化；实施时逐分支画真值表核对一次）。

## E6 🟢 ScriptEditorPanel.cpp —— "另存为..."重复 SaveDocument 逻辑

- **位置**：`Editor/src/Panels/ScriptEditorPanel.cpp:886-902`
- **类别**：冗余代码
- **问题**：工具栏另存为内联重写了 SaveDocument 的写文件 + 更新 Path/DisplayName/Dirty 三件事。
- **简化方案**：`SaveDocument` 拆出 `bool WriteDocument(Document&, const std::filesystem::path&)`；另存为 = 选路径 + `WriteDocument`。
- **预计收益**：约 12 行
- **风险**：🟢 低。

## E7 🟢 ABSourceLabel 转发包装双份

- **位置**：`Editor/src/EditorPanelCoordinator.cpp:24-28`、`Editor/src/Panels/RenderSettingsPanel.cpp:16-18`
- **类别**：过度设计
- **问题**：两个文件各自包了一个一模一样的 `ABSourceLabel`，只是转发 `ParticleSystemGPU::ABConfigSourceLabel`（已 grep 确认共 4 个调用点 + 2 个定义）。
- **简化方案**：两处直接调用 `ParticleSystemGPU::ABConfigSourceLabel(...)`，删除包装。
- **预计收益**：约 8 行
- **风险**：🟢 无。

---

# T. benchmark/ 脚本

## T1 🟡 benchmark/tmp_analysis.py —— 未跟踪临时脚本

- **位置**：`benchmark/tmp_analysis.py`（git `??`，232 行）
- **类别**：死代码
- **问题**：一次性分析脚本，默认路径指向已作废的 `results_v3/run_20260831_110725`（SPEC.md 已判该轮作废），重复实现 summarize.py 的分位数/均值逻辑，无任何脚本/文档引用。
- **简化方案**：二选一（**待用户拍板，P3**）：
  - 直接删除（分析结论如有价值先摘录进 SPEC.md）；
  - 移入 `benchmark/archive/` 并在文件头注明一次性用途。
- **预计收益**：232 行（退出跟踪视野）
- **风险**：🟢 低；该文件未提交，删除不影响任何历史。

## T2 🟢 benchmark/compare_v2_v3.py —— 一次性比较脚本默认值硬编码

- **位置**：`benchmark/compare_v2_v3.py:7-9`
- **类别**：过度设计（可维护性）
- **问题**：默认 target 硬编码特定 run 目录（`run_20260831_174549`）、base 默认在仓库外 `results_v2/`；与 summarize.py 统计逻辑部分重叠。
- **简化方案**：**不删**（SPEC.md 引用它作为结案工具链）；仅把 `DEFAULT_*` 改为必填 argparse 参数，或在文件头 docstring 标注"一次性结案工具，默认值对应定稿轮"。
- **预计收益**：可维护性（~40 行可并入 summarize.py `--compare` 模式，但不必现在做）
- **风险**：🟢 低（改动默认值前先在 SPEC 记录口径，避免破坏"复现定稿图表"路径）。

## T3 🟡 run_matrix.ps1 / run_cooled.ps1 —— 结果校验逻辑两份

- **位置**：`benchmark/run_matrix.ps1:36-80`（Test-CompleteResult）；run_cooled.ps1 内部分段重试同样解析 CSV 完整性
- **类别**：冗余代码
- **问题**：两个 .ps1 各自实现 CSV 行数/字段/Run-Frame 唯一性校验（约 60 行/份）。
- **简化方案**：抽 `benchmark/common.psm1`（dot-source），只放 `Test-CompleteResult` + `Get-GpuState`；两个入口脚本保留各自编排逻辑（**勿合并入口**，run_cooled 是温控分段结案工具）。
- **预计收益**：约 50 行
- **风险**：🟡 中——基准战役已结案（定稿轮 run_20260831_174549）；**若 D-24/D-25 优化后还要重采，届时再动收益更大**。若现在动，必须带 `-DryRun` 与一次短跑验证（PowerShell 5.1 需 UTF-8 BOM——见既有教训）。

---

# 本文档小计

| 区域 | 条目 | 预计行数 | 低风险部分 |
|---|---|---|---|
| Editor | E1-E7 | 500-700 | ~90（E4-E7） |
| benchmark | T1-T3 | 280-330 | ~240 |
| **合计** | 10 条 | **780-1030** | **~330** |

优先动手顺序：E7/E5/E6（零风险小件）→ E4（保存三胞胎）→ T1（待拍板后删/归档）→ T2（标注）→ E1（资产字段收敛，UI 目检）→ E2（Vec3 合并）→ E3（拍板裁剪档位）→ T3（推迟到下次重采前）。
