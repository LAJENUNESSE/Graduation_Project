-- 第一人称控制器：WASD 移动 + 鼠标旋转 + 空格跳跃
-- 挂载方式：添加 NativeScriptComponent → Lua 后端 → 选择此脚本
-- 要求实体有 TransformComponent（编辑器自动添加）
-- 使用方法：运行时锁定鼠标，点击窗口聚焦后用 WASD 移动，鼠标旋转视角，空格跳跃

local script = {}

script.MoveSpeed = 5.0       -- 移动速度（单位/秒）
script.LookSpeed = 0.2       -- 鼠标灵敏度
script.JumpForce = 8.0       -- 跳跃力度
script.Gravity = -20.0       -- 重力加速度

script._VelocityY = 0.0      -- 内部：Y 轴速度
script._IsGrounded = true    -- 内部：是否在地面

function script:OnCreate()
    Engine.Info("FirstPersonController OnCreate")
end

function script:OnUpdate(dt)
    local x, y, z = self.Entity:GetTranslation()

    -- 鼠标旋转
    local rx, ry, rz = self.Entity:GetRotation()
    local mx, my = Engine.GetMousePosition()

    -- 简单旋转控制（按住左键拖动）
    if Engine.IsMouseButtonPressed(Mouse.MOUSE_LEFT) then
        ry = ry + mx * self.LookSpeed * 10.0 * dt
        rx = rx + my * self.LookSpeed * 10.0 * dt
        -- 限制俯仰角度
        rx = math.max(-89, math.min(89, rx))
        self.Entity:SetRotation(rx, ry, rz)
    end

    -- 重力
    if not self._IsGrounded then
        self._VelocityY = self._VelocityY + self.Gravity * dt
    end

    -- 跳跃
    if Engine.IsKeyPressed(Key.KEY_SPACE) and self._IsGrounded then
        self._VelocityY = self.JumpForce
        self._IsGrounded = false
    end

    -- 落地检测（Y 接近 0）
    y = y + self._VelocityY * dt
    if y <= 0.0 then
        y = 0.0
        self._VelocityY = 0.0
        self._IsGrounded = true
    end
    self.Entity:SetTranslation(x, y, z)

    -- WASD 移动（基于前向向量）
    local fx, fy, fz = self.Entity:GetForward()
    local moveX, moveZ = 0.0, 0.0

    if Engine.IsKeyPressed(Key.KEY_W) then
        moveX = moveX - fx * self.MoveSpeed * dt
        moveZ = moveZ - fz * self.MoveSpeed * dt
    end
    if Engine.IsKeyPressed(Key.KEY_S) then
        moveX = moveX + fx * self.MoveSpeed * dt
        moveZ = moveZ + fz * self.MoveSpeed * dt
    end
    if Engine.IsKeyPressed(Key.KEY_A) then
        -- 垂直于前向的向量 (fz, 0, -fx)
        moveX = moveX - fz * self.MoveSpeed * dt
        moveZ = moveZ + fx * self.MoveSpeed * dt
    end
    if Engine.IsKeyPressed(Key.KEY_D) then
        moveX = moveX + fz * self.MoveSpeed * dt
        moveZ = moveZ - fx * self.MoveSpeed * dt
    end

    x = x + moveX
    z = z + moveZ
    self.Entity:SetTranslation(x, y, z)
end

function script:OnDestroy()
    Engine.Info("FirstPersonController OnDestroy")
end

return script
