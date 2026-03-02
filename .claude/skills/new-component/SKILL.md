---
name: new-component
description: 交互式创建新的 ECS 组件，自动生成反射注册代码
disable-model-invocation: true
allowed-tools: Read, Edit, Grep
argument-hint: "[ComponentName] 组件名称（PascalCase，如 WindZoneComponent）"
---

创建一个新的 ECS 组件，自动完成反射系统注册。

## 流程

### 1. 收集组件信息

向用户询问以下信息（如果参数中未提供）：

- **组件名称**（PascalCase，必须以 `Component` 结尾，如 `WindZoneComponent`）
- **显示名称**（中文，如 "风场"，用于编辑器 UI）
- **属性列表**，每个属性需要：
  - 字段名（PascalCase）
  - C++ 类型（float, int, bool, glm::vec3 等）
  - 显示标签（中文）
  - 默认值

### 2. 修改文件

按以下顺序修改三处代码：

**a) `Engine/src/Scene/Components.h`** — 添加组件结构体 + 反射宏声明

在文件末尾（最后一个组件之后）添加：
```cpp
struct XxxComponent
{
    // 属性 + 默认值
};

ENGINE_COMPONENT(XxxComponent, "显示名称");
ENGINE_PROPERTY(XxxComponent, FieldName, "标签", PropertyType::类型);
```

**b) `Engine/src/Reflection/ComponentRegistry.cpp`** — 添加注册代码

在最后一个 `REGISTER_COMPONENT_END` 之后添加：
```cpp
REGISTER_COMPONENT_BEGIN(XxxComponent)
    REGISTER_PROPERTY(XxxComponent, FieldName)
REGISTER_COMPONENT_END(XxxComponent)
```

### 3. 验证

- 确认 Components.h 中结构体有默认构造
- 确认每个 ENGINE_PROPERTY 都有对应的 REGISTER_PROPERTY
- 确认 PropertyType 与 C++ 类型匹配

### 属性类型速查

| C++ 类型 | PropertyType |
|---------|-------------|
| float | Float |
| int | Int |
| bool | Bool |
| glm::vec2 | Vec2 |
| glm::vec3 | Vec3 |
| glm::vec4 | Vec4 |
| glm::vec3 (颜色) | Color3 |
| std::string | String |
| std::string (路径) | AssetPath |
| enum | Enum |
