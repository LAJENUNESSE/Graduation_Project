---
name: component-scaffolder
description: 为引擎快速脚手架一个带反射注册的 ECS 组件
metadata:
  short-description: 组件脚手架生成
---

面向“新增组件”场景的快速脚手架流程。该 skill 偏向一次性完整生成，适合直接落地代码修改。

## 流程

1. 收集组件信息：
- 名称（必须以 `Component` 结尾）
- 显示名（中文）
- 属性（名称、类型、默认值、显示标签）

2. 修改 `Engine/src/Scene/Components.h`：
- 添加组件结构体
- 添加 `ENGINE_COMPONENT` 与多条 `ENGINE_PROPERTY`

3. 修改 `Engine/src/Reflection/ComponentRegistry.cpp`：
- 添加 `REGISTER_COMPONENT_BEGIN/END`
- 为每条属性添加 `REGISTER_PROPERTY`

4. 必要校验：
- 确认 `ComponentRegistry.cpp` 已包含 `Scene/Components.h`（通常已有）
- 组件结构体具备默认构造与可拷贝语义
- 每个 `ENGINE_PROPERTY` 都有对应 `REGISTER_PROPERTY`
- `PropertyType` 与字段 C++ 类型严格匹配

5. 自检：
- 默认构造/可拷贝
- 反射声明与注册一一对应
- `PropertyType` 与字段类型一致

## 类型映射速查

- `float -> PropertyType::Float`
- `int -> PropertyType::Int`
- `bool -> PropertyType::Bool`
- `glm::vec2 -> PropertyType::Vec2`
- `glm::vec3 -> PropertyType::Vec3`（颜色可用 `Color3`）
- `glm::vec4 -> PropertyType::Vec4`（颜色可用 `Color4`）
- `std::string -> PropertyType::String`（路径可用 `AssetPath`）
- `enum -> PropertyType::Enum`（需要额外枚举名数组）

## 可选 Hints 示例

```cpp
ENGINE_PROPERTY_HINTS(MyComp, Speed, "速度", PropertyType::Float,
    { .Speed = 0.1f, .Min = 0.0f, .Max = 100.0f });
```

## 输出要求

- 给出新增代码片段与插入位置。
- 给出最小验证步骤（编译 + 编辑器中 Add Component 可见性）。
