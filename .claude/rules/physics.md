---
paths:
  - "Engine/src/Physics/**"
---

# Physics（物理系统）

双后端物理：Bullet3（生产用）+ 自研简易求解器（教学用）。

## Bullet3 后端（BulletPhysicsWorld）

- 初始化时设置重力
- 从 RigidBodyComponent + Collider 组件创建刚体
- `Step()` 推进模拟 → `SyncToECS()` 回写 Transform
- 运动学体（Kinematic）需双向同步：`SyncFromECS()` → Step → `SyncToECS()`
- 碰撞事件通过 `GetCollisionEvents()` 获取，仅在当前帧有效

## 刚体类型

| 类型 | 质量 | 行为 |
|------|------|------|
| Static | 0 | 不移动，纯碰撞体 |
| Dynamic | >0 | 受力运动 |
| Kinematic | 0（特殊标记） | 由代码驱动位置 |

## 注意事项

- 每个 Scene 只选一种后端，不可混用，切换需要完整清理
- 碰撞体形状变化时必须销毁重建 Bullet 刚体（开销较大）
- `Scene::DestroyEntity()` 必须先清理对应的 Bullet 刚体
- 碰撞事件每帧清空，仅在 `Step()` 后立即可用
- 自研求解器仅支持 AABB/球体碰撞，不建议生产使用
