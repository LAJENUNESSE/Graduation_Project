# Lua 脚本 API 参考

本文档描述引擎为 Lua 脚本暴露的 API。Lua 脚本以 `.lua` 文件形式存在于 `assets/scripts/` 目录，通过编辑器的 **NativeScriptComponent → Lua 后端** 挂载到实体。

---

## 脚本结构

每个 Lua 脚本必须返回一个包含生命周期函数的表：

```lua
local script = {}

function script:OnCreate()
    -- 实体被创建时调用（Play 模式启动时）
end

function script:OnUpdate(dt)
    -- 每帧调用，dt 为自上一帧以来的时间（秒）
end

function script:OnDestroy()
    -- 实体被销毁前调用（Play 模式停止时）
end

return script
```

---

## Entity API

通过 `self.Entity` 访问实体，所有方法返回/接受浮点数。

### Transform（变换）

| Lua 方法 | 说明 | 返回值 / 参数 |
|----------|------|---------------|
| `Entity:GetTranslation()` | 获取局部位置（相对于父实体） | `x, y, z`（米） |
| `Entity:SetTranslation(x, y, z)` | 设置局部位置 | 传入 x, y, z |
| `Entity:GetWorldTranslation()` | 获取世界位置 | `x, y, z`（米） |
| `Entity:GetRotation()` | 获取局部欧拉旋转角 | `x, y, z`（**度数**） |
| `Entity:SetRotation(x, y, z)` | 设置局部欧拉旋转角 | 传入 x, y, z（**度数**） |
| `Entity:GetWorldRotation()` | 获取世界欧拉旋转角 | `x, y, z`（**度数**） |
| `Entity:GetScale()` | 获取缩放 | `x, y, z`（倍数） |
| `Entity:SetScale(x, y, z)` | 设置缩放 | 传入 x, y, z |
| `Entity:GetForward()` | 获取世界前向向量（归一化） | `x, y, z` |
| `Entity:Translate(dx, dy, dz)` | 相对位移（局部坐标） | 在当前位置上加 |
| `Entity:Rotate(dx, dy, dz)` | 相对旋转（局部坐标） | 在当前角度上加（度数） |

### 实体操作

| Lua 方法 | 说明 |
|----------|------|
| `Entity:GetName()` | 获取实体名称（TagComponent） |
| `Entity:DestroySelf()` | 销毁自身实体（立即移除） |
| `Entity:DistanceTo(other)` | 计算到另一实体的距离（米） |

---

## Engine API

通过 `Engine` 全局表访问。

### 日志

| Lua 方法 | 说明 |
|----------|------|
| `Engine.Info(msg)` | 输出 Info 级别日志（绿色） |
| `Engine.Warn(msg)` | 输出 Warning 级别日志（黄色） |
| `Engine.Error(msg)` | 输出 Error 级别日志（红色） |
| `Engine.Debug(msg)` | 输出 Debug 级别日志（白色） |

### 输入

| Lua 方法 | 说明 | 返回值 |
|----------|------|--------|
| `Engine.IsKeyPressed(keyCode)` | 检查键盘按键是否按下 | `true` / `false` |
| `Engine.IsMouseButtonPressed(button)` | 检查鼠标按键是否按下 | `true` / `false` |
| `Engine.GetMousePosition()` | 获取鼠标屏幕坐标 | `x, y`（像素） |

---

## 常量表

### Key（键盘按键）

```lua
Key.KEY_W         -- W
Key.KEY_A         -- A
Key.KEY_S         -- S
Key.KEY_D         -- D
Key.KEY_Q         -- Q
Key.KEY_E         -- E
Key.KEY_SPACE     -- 空格
Key.KEY_ESCAPE    -- Escape
Key.KEY_UP        -- 上箭头
Key.KEY_DOWN      -- 下箭头
Key.KEY_LEFT      -- 左箭头
Key.KEY_RIGHT     -- 右箭头
Key.KEY_SHIFT     -- Shift
Key.KEY_CTRL      -- Ctrl
Key.KEY_F1 ~ KEY_F12  -- 功能键
```

### Mouse（鼠标按键）

```lua
Mouse.MOUSE_LEFT    -- 左键
Mouse.MOUSE_RIGHT   -- 右键
Mouse.MOUSE_MIDDLE  -- 中键（滚轮按下）
```

---

## 示例脚本

### 1. 持续旋转（rotate.lua）

```lua
local script = {}

script.RotationSpeed = 45.0  -- 度/秒

function script:OnCreate()
    Engine.Info("rotate.lua OnCreate")
end

function script:OnUpdate(dt)
    local rx, ry, rz = self.Entity:GetRotation()
    self.Entity:SetRotation(rx, ry + self.RotationSpeed * dt, rz)
end

function script:OnDestroy()
    Engine.Info("rotate.lua OnDestroy")
end

return script
```

### 2. WASD 移动（FirstPersonController.lua）

```lua
local script = {}

script.MoveSpeed = 5.0
script.RotationSpeed = 45.0

function script:OnUpdate(dt)
    local fx, fy, fz = self.Entity:GetForward()
    local x, y, z = self.Entity:GetTranslation()

    if Engine.IsKeyPressed(Key.KEY_W) then
        self.Entity:Translate(-fx * script.MoveSpeed * dt, 0, -fz * script.MoveSpeed * dt)
    end
    if Engine.IsKeyPressed(Key.KEY_S) then
        self.Entity:Translate(fx * script.MoveSpeed * dt, 0, fz * script.MoveSpeed * dt)
    end
    if Engine.IsKeyPressed(Key.KEY_A) then
        self.Entity:Translate(-fz * script.MoveSpeed * dt, 0, fx * script.MoveSpeed * dt)
    end
    if Engine.IsKeyPressed(Key.KEY_D) then
        self.Entity:Translate(fz * script.MoveSpeed * dt, 0, -fx * script.MoveSpeed * dt)
    end
    if Engine.IsKeyPressed(Key.KEY_Q) then
        local rx, ry, rz = self.Entity:GetRotation()
        self.Entity:SetRotation(rx, ry - script.RotationSpeed * dt, rz)
    end
    if Engine.IsKeyPressed(Key.KEY_E) then
        local rx, ry, rz = self.Entity:GetRotation()
        self.Entity:SetRotation(rx, ry + script.RotationSpeed * dt, rz)
    end
end

return script
```

### 3. 漂浮动画（Floater.lua）

```lua
local script = {}

script.Amplitude = 0.5
script.Frequency = 1.5

script._StartY = 0.0
script._Time = 0.0

function script:OnCreate()
    local _, y, _ = self.Entity:GetTranslation()
    self._StartY = y
end

function script:OnUpdate(dt)
    self._Time = self._Time + dt
    local x, y, z = self.Entity:GetTranslation()
    local offsetY = self.Amplitude * math.sin(2.0 * math.pi * self.Frequency * self._Time)
    self.Entity:SetTranslation(x, self._StartY + offsetY, z)
end

return script
```

---

## 当前限制

以下功能在 C++ 引擎中已实现，但尚未暴露给 Lua：

| 功能 | 说明 |
|------|------|
| 组件增删查 | `AddComponent`/`GetComponent`/`HasComponent` |
| 碰撞回调 | `OnCollisionEnter`/`OnCollisionExit` |
| 触发器回调 | `OnTriggerEnter`/`OnTriggerExit` |
| 子实体 API | `GetChildren`/`SetParent` |
| 时间缩放 | `TimeScale` |

如需这些功能，需要扩展 `LuaScriptEngine.cpp` 添加更多 C 绑定。

---

## 编辑器使用方法

1. 在编辑器中选择一个实体
2. 在属性面板中点击 **Add Component**
3. 选择 **NativeScriptComponent**（脚本）
4. 在脚本后端中选择 **Lua**
5. 在 "Lua 脚本" 字段输入路径（如 `assets/scripts/rotate.lua`），或点击 **浏览** 按钮选择
6. 也可将 `.lua` 文件从资源浏览器**拖放到**编辑器视口中
7. 点击 **Play** 进入运行模式，脚本将开始执行
