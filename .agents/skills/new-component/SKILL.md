---
name: new-component
description: 交互式创建新的 ECS 组件，自动生成反射注册代码
metadata:
  short-description: 创建并注册 ECS 组件
---

创建一个新的 ECS 组件，并完成反射系统注册。

## 流程

### 1. 收集组件信息

向用户询问以下信息（如参数中未提供）：

- **组件名称**（PascalCase，**必须**以 `Component` 结尾，如 `WindZoneComponent`）
- **显示名称**（中文，用于编辑器 UI，如 "风场"）
- **属性列表**，每个属性需要：
  - 字段名（PascalCase）
  - C++ 类型（`float`、`int`、`bool`、`glm::vec3` 等）
  - 显示标签（中文）
  - 默认值
  - 可选 UI 约束（Min / Max / Speed / DragSpeed）

**遇到不确定或缺失信息时主动向用户提问，不要猜测。**

### 2. 修改文件

按顺序修改两处代码：

#### a) `Engine/src/Scene/Components.h`

在文件末尾（最后一个组件之后）添加结构体 + 反射宏声明：

```cpp
struct XxxComponent
{
    // 属性字段 + 默认值
};

ENGINE_COMPONENT(XxxComponent, "显示名称");
ENGINE_PROPERTY(XxxComponent, FieldName, "标签", PropertyType::类型);
```

如需 UI 约束（数值 Min/Max/Speed），使用带 Hints 的变体：

```cpp
ENGINE_PROPERTY_HINTS(XxxComponent, Speed, "速度", PropertyType::Float,
    { .Speed = 0.1f, .Min = 0.0f, .Max = 100.0f });
```

#### b) `Engine/src/Reflection/ComponentRegistry.cpp`

在最后一个 `REGISTER_COMPONENT_END` 之后添加：

```cpp
REGISTER_COMPONENT_BEGIN(XxxComponent)
    REGISTER_PROPERTY(XxxComponent, FieldName)
REGISTER_COMPONENT_END(XxxComponent)
```

### 3. 自检

- 结构体具备**默认构造**与**可拷贝**语义
- 每个 `ENGINE_PROPERTY` / `ENGINE_PROPERTY_HINTS` 都有对应的 `REGISTER_PROPERTY`
- `PropertyType` 与字段 C++ 类型严格匹配
- `ComponentRegistry.cpp` 已包含 `Scene/Components.h`（通常已有）

### 4. 构建验证

执行 `build` skill 构建 `Editor` 目标，确认编译通过。构建成功后，编辑器的 Add Component 菜单应出现新组件。

## 属性类型速查

| C++ 类型 | PropertyType |
|---------|-------------|
| `float` | `Float` |
| `int` | `Int` |
| `bool` | `Bool` |
| `glm::vec2` | `Vec2` |
| `glm::vec3` | `Vec3` |
| `glm::vec4` | `Vec4` |
| `glm::vec3`（颜色） | `Color3` |
| `glm::vec4`（颜色） | `Color4` |
| `std::string` | `String` |
| `std::string`（路径） | `AssetPath` |
| `enum` | `Enum`（需额外枚举名数组） |
