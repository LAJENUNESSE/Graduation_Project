-- 持续旋转脚本：每帧绕 Y 轴旋转
-- 挂载方式：添加 NativeScriptComponent → Lua 后端 → 选择此脚本
-- 可在 OnCreate 中修改 RotationSpeed 修改旋转速度（默认 45 度/秒）

local script = {}

script.RotationSpeed = 45.0  -- 度/秒

function script:OnCreate()
    Engine.Info("rotate.lua OnCreate, speed=" .. self.RotationSpeed .. " deg/s")
end

function script:OnUpdate(dt)
    -- 绕 Y 轴旋转（弧度）
    local rx, ry, rz = self.Entity:GetRotation()
    self.Entity:SetRotation(rx, ry + self.RotationSpeed * dt, rz)
end

function script:OnDestroy()
    Engine.Info("rotate.lua OnDestroy")
end

return script

