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
- 碰撞体偏移使用 `btCompoundShape` 包装实现

## 自研求解器（PhysicsWorld）

- 碰撞检测使用**世界坐标**（非本地坐标）
- 惯性张量为 `mat3`（非标量），支持各向异性
- 角动量积分包含**陀螺力矩项**（`τ_gyro = ω × (I · ω)`）
- 四元数积分避免万向节锁

## CollisionMath（独立命名空间）

从 PhysicsWorld 提取的碰撞检测纯数学函数，可独立单元测试：

- `SphereSphere` — 球-球碰撞
- `AABBAABB` — AABB-AABB 碰撞（接触面计算，非中心中点）
- `OBBOBB` — 完整 OBB-OBB SAT 分离轴检测
- `SphereOBB` — 球-OBB 碰撞

输出 `CollisionInfo`：contactPoint / contactNormal / penetrationDepth

## SDFMath（header-only）

从 CudaSPHPipeline 提取的 SDF 纯数学函数，兼容 CUDA `__device__` 和 C++：

- `BoxSDF` — 点到 AABB 有符号距离
- `SphereSDF` — 点到球心有符号距离

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
- 自研求解器支持 Sphere/AABB/OBB 碰撞（含 SAT），用于教学演示
- 新增碰撞数学函数应放在 `CollisionMath` 命名空间，保持可测试性
