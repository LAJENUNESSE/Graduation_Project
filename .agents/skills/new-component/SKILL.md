---
name: new-component
description: 交互式创建 ECS 组件并补齐反射注册
metadata:
  short-description: 创建并注册 ECS 组件
---

创建新的 ECS 组件，并完成反射系统注册。

## 信息收集

需要明确以下字段：

- 组件名（PascalCase，且以 `Component` 结尾）
- 显示名（中文）
- 属性列表：字段名、C++ 类型、显示标签、默认值

## 修改位置

1. `Engine/src/Scene/Components.h`
- 新增组件结构体
- 增加 `ENGINE_COMPONENT` / `ENGINE_PROPERTY` 声明

2. `Engine/src/Reflection/ComponentRegistry.cpp`
- 新增 `REGISTER_COMPONENT_BEGIN/END`
- 为每个属性添加 `REGISTER_PROPERTY`

## 约束

- 结构体必须有默认构造与可拷贝语义。
- `PropertyType` 必须匹配 C++ 字段类型。
- 每个 `ENGINE_PROPERTY` 必须有对应 `REGISTER_PROPERTY`。

## 类型映射速查

- `float -> PropertyType::Float`
- `int -> PropertyType::Int`
- `bool -> PropertyType::Bool`
- `glm::vec2 -> PropertyType::Vec2`
- `glm::vec3 -> PropertyType::Vec3`（颜色场景可用 `Color3`）
- `glm::vec4 -> PropertyType::Vec4`
- `std::string -> PropertyType::String`（路径可用 `AssetPath`）
- `enum -> PropertyType::Enum`
