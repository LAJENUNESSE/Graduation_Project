---
paths:
  - "Engine/src/Physics/**"
---

# Physics（物理系统）

主用 Bullet3（生产用），辅以 SDFMath 纯数学库。

## Bullet3 后端（BulletPhysicsWorld）

- 初始化时设置重力
- 从 RigidBodyComponent + Collider 组件创建刚体
- `Step()` 推进模拟 → `SyncToECS()` 回写 Transform
- 运动学体（Kinematic）需双向同步：`SyncFromECS()` → Step → `SyncToECS()`
- 碰撞事件通过 `GetCollisionEvents()` 获取，仅在当前帧有效
- 碰撞体偏移使用 `btCompoundShape` 包装实现

## PhysicsDebugDraw

继承 `btIDebugDraw`，把 Bullet 的 debug 几何（接触点、AABB、约束等）转发到引擎的 line renderer（走 `RenderCommand::DrawLines`，与具体后端无关）。

## SDFMath（header-only）

`SDFMath.h` —— SDF 纯数学函数，原本从 CUDA SPH 管线提取，目前作为 header-only 库用于 SPH 边界条件与单元测试：

- `BoxSDF` — 点到 AABB 有符号距离 + 梯度法线
- `SphereSDF` — 点到球心有符号距离 + 梯度法线

历史上为 CUDA `__device__` 兼容保留 `SDF_DEVICE` 宏（`__CUDACC__` 触发），CUDA sidecar 已下线但宏保留以便未来复用。

## 刚体类型

| 类型 | 质量 | 行为 |
|------|------|------|
| Static | 0 | 不移动，纯碰撞体 |
| Dynamic | >0 | 受力运动 |
| Kinematic | 0（特殊标记） | 由代码驱动位置 |

## 注意事项

- 每个 Scene 持有一个 Bullet `btDiscreteDynamicsWorld`，切换场景时需完整清理
- 碰撞体形状变化时必须销毁重建 Bullet 刚体（开销较大）
- `Scene::DestroyEntity()` 必须先清理对应的 Bullet 刚体
- 碰撞事件每帧清空，仅在 `Step()` 后立即可用
- 新增 SDF / 碰撞数学函数应放在 header-only 文件并保持纯数学（无引擎依赖），便于单元测试
