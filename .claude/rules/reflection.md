---
paths:
  - "Engine/src/Reflection/**"
  - "Engine/src/Scene/Components.h"
---

# Reflection（反射系统）

编译期反射宏驱动的组件注册、自动序列化和 Inspector 生成。

## 新组件注册两步走

### 1. Components.h 定义组件结构体

在 `Engine/src/Scene/Components.h` 中添加 POD 结构体（必须有默认构造和拷贝构造）。

### 2. ComponentRegistry.cpp 注册反射元数据

在 `Engine/src/Reflection/ComponentRegistry.cpp` 中添加：
- `ENGINE_COMPONENT(MyComponent, "显示名称")` 声明显示名称
- `ENGINE_PROPERTY(MyComponent, FieldName, "标签", Vec3)` 声明属性
- `REGISTER_COMPONENT_BEGIN(MyComponent) ... REGISTER_COMPONENT_PROPERTY(...) ... REGISTER_COMPONENT_END(MyComponent)` 注册

示例：

```cpp
// ComponentRegistry.cpp 中声明
ENGINE_COMPONENT(MyComponent, "显示名称");
ENGINE_PROPERTY(MyComponent, FieldName, "标签", Vec3);
// 可选 hints（见 PropertyTypes.h:24-41）：Min, Max, Speed, Format,
// Group, ReadOnly, Transient；Enum 用 EnumNames + EnumCount；
// AssetPath 用 FileFilter + FileDesc（经 ENGINE_PROPERTY_EX 设置）

// 注册
REGISTER_COMPONENT_BEGIN(MyComponent)
    REGISTER_COMPONENT_PROPERTY(MyComponent, FieldName)
REGISTER_COMPONENT_END(MyComponent)
```

### 3. 自动生效

注册后组件会自动出现在编辑器 Add Component 菜单、属性面板（AutoInspector）和场景序列化（AutoSerializer）中。

## 核心类

- **ComponentRegistry** — 单例，存储所有 `ComponentMeta`（类型擦除的 Has/Add/Get/Remove/Copy lambda + 属性元数据）
- **AutoInspector** — 根据反射元数据自动生成 ImGui 属性面板
- **AutoSerializer** — 根据反射元数据驱动 YAML 场景读写
- **PropertyInfo** — 属性描述符（名称、类型、偏移量、hints）
- **PropertyTypes.h** — 属性类型枚举（Float, Int, UInt32, Bool, String, Vec2-4, Color3/4, Enum, AssetPath）

## 注意事项

- `ENGINE_COMPONENT` / `ENGINE_PROPERTY` 宏现在在 `.cpp`（ComponentRegistry.cpp）中声明，不再放在头文件
- 宏展开会创建命名空间 `_Reflect_##CompType`，内含 inline 元数据
- `PropertyType` 枚举必须与结构体成员实际类型匹配，无隐式转换
- Enum 属性需要外部定义 `const char* s_EnumNames[]` 数组并设置 `hints.EnumCount`（如 `ComponentRegistry.cpp:87`）
- `hints.Transient = true` 的属性跳过序列化（如 RigidBody 的 LinearVelocity/Force 等，`ComponentRegistry.cpp:101-104`）
- 未注册的组件不会出现在编辑器中，也不会被序列化
