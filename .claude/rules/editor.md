---
paths:
  - "Editor/**"
---

# Editor

可视化编辑器可执行目标，链接 Engine 静态库。

## 结构

- **EditorApp.cpp** — 入口，创建 `EditorApplication`，推入 `EditorLayer`
- **EditorLayer** — 主编辑器逻辑：视口渲染、ImGuizmo Gizmo、Play/Stop 模式、场景 I/O
- **Panels/** — UI 面板（场景层级树、属性 Inspector）
- **Scripts/** — NativeScript 示例脚本

## EditorLayer 关键流程

- 管理两个场景指针：`m_EditorScene`（编辑态）和 `m_ActiveScene`（运行态切换用）
- 持有两个 Framebuffer：标准（RGBA8 + RED_INTEGER + Depth）和 HDR（RGBA16F）
- 实体拾取：通过 RED_INTEGER 附件读取鼠标位置下的 Entity ID
- Gizmo：ImGuizmo 处理平移/旋转/缩放操控

## 面板

| 面板 | 类 | 窗口标题 |
|------|-----|----------|
| 场景层级 | SceneHierarchyPanel | "场景层级" |
| 属性面板 | PropertiesPanel | "属性" |

### PropertiesPanel 组件绘制策略

1. **手动硬编码**：Transform、Camera、Light 等核心组件用 `DrawComponent<T>()` 模板 + lambda
2. **反射自动绘制**：遍历 `ComponentRegistry` 注册的组件，通过 `AutoInspector::Draw()` 渲染
3. **脚本特殊处理**：NativeScriptComponent 用 ComboBox 选择已注册脚本

## NativeScript 编写规范

```cpp
// Scripts/MyScript.h
class MyScript : public ScriptableEntity {
public:
    float Speed = 5.0f;                    // 公开属性
    void OnCreate() override { }           // 场景播放时调用
    void OnUpdate(Timestep ts) override {  // 每帧调用
        auto& t = GetTransform();
        if (IsKeyPressed(KeyCode::W))
            t.Translation.z -= Speed * ts;
    }
    void OnDestroy() override { }          // 场景停止时调用
};

// Scripts/MyScript.cpp
REGISTER_SCRIPT(MyScript, "我的脚本")  // 第二个参数是编辑器显示名
```

## 注意事项

- 所有 UI 文字使用中文
- 实体删除必须延迟到迭代外执行，避免迭代器失效
- 旋转在 Inspector 中显示为角度（度），存储为弧度
- 纹理路径必须在项目目录内（校验无 `..`）
