---
paths:
  - "Editor/**"
---

# Editor

可视化编辑器可执行目标，链接 Engine 静态库。采用**分层协调式架构**。

## 架构

```
EditorLayer (主协调器)
├── EditorBootstrapper            — DI 工厂，创建并连线所有子系统
├── EditorSceneSession            — 场景生命周期（Edit ↔ Play）、序列化
├── EditorShell                   — ImGui Dockspace + 快捷键 → 产出 EditorShellActions
├── EditorPanelCoordinator        — 聚合 7 个面板、管理选择状态（单选+多选）
├── EditorRenderController        — SceneRenderer + PostProcessing + MSAA
├── EditorViewportController      — EditorCamera + 3 个 Framebuffer（标准/HDR/拾取）
├── EditorSelectionGizmoController — ImGuizmo Gizmo 操控 + 悬停拾取
├── CommandHistory                — 纯逻辑双栈 Undo/Redo，最多 100 项
├── UndoSystem                    — 6 种具体 ICommand 实现
└── EditorAssetDescriptor         — 资源类型 → UI 图标 + 拖拽 Payload 映射
```

### EditorBootstrapper

对象工厂与依赖注入中心：
- `Assemble()` — 创建所有子系统（基础设施 → 面板 → 控制器）
- `Teardown()` — 逆序清理
- 回调设置接口（SceneOpen、MSAAChanged、Modified）

## 面板

| 面板 | 类 | 窗口标题 |
|------|-----|----------|
| 场景层级 | SceneHierarchyPanel | "场景层级" |
| 属性面板 | PropertiesPanel + PropertiesPanelCustomDrawers | "属性" |
| 控制台 | ConsolePanel | "控制台" |
| 资源浏览器 | AssetBrowserPanel | "资源浏览器" |
| 渲染设置 | RenderSettingsPanel | "渲染设置" |
| 脚本编辑器 | ScriptEditorPanel | "脚本编辑器" |

### PropertiesPanel 组件绘制策略

1. **手动硬编码**：Transform、Camera、Light 等核心组件用 `DrawComponent<T>()` 模板 + lambda
2. **自定义绘制器**：复杂组件（MeshRenderer、Terrain、Particle、Audio、Video）由 `PropertiesPanelCustomDrawers` 提供专用 UI
3. **反射自动绘制**：遍历 `ComponentRegistry` 注册的组件，通过 `AutoInspector::Draw()` 渲染
4. **脚本特殊处理**：NativeScriptComponent 用 ComboBox 选择已注册脚本

## CommandHistory + UndoSystem

`CommandHistory`（纯逻辑，从 UndoSystem 拆分）管理双栈和执行逻辑，可独立单元测试。

ICommand 接口（Execute/Undo/GetDescription），6 种命令类型：
- TransformChangeCommand / MultiTransformChangeCommand
- EntityCreateCommand / EntityDeleteCommand
- PropertyChangeCommand（类型擦除泛型，std::any）
- ParentChangeCommand

## NativeScript 编写规范

```cpp
// Scripts/MyScript.h
class MyScript : public ScriptableEntity {
public:
    float Speed = 5.0f;
    void OnCreate() override { }
    void OnUpdate(Timestep ts) override {
        auto& t = GetTransform();
        if (IsKeyPressed(KeyCode::W))
            t.Translation.z -= Speed * ts;
    }
    void OnDestroy() override { }
};

// Scripts/MyScript.cpp
REGISTER_SCRIPT(MyScript, "我的脚本")
```

现有示例脚本：PlayerController、RotateScript、CollisionTestScript

## 注意事项

- 所有 UI 文字使用中文
- 实体删除必须延迟到迭代外执行，避免迭代器失效
- 旋转在 Inspector 中显示为角度（度），存储为弧度
- 纹理路径必须在项目目录内（校验无 `..`）
- EditorShell 产出 Actions 结构体，EditorLayer 处理 — 不要在 Shell 中直接执行逻辑
- 新面板应通过 EditorPanelCoordinator 注册，不要直接在 EditorLayer 中管理
- 新子系统应通过 EditorBootstrapper 创建和注入，不要在 EditorLayer 中直接 new
