-- 漂浮动画：实体按 Sin 波上下起伏
-- 挂载方式：添加 NativeScriptComponent → Lua 后端 → 选择此脚本
-- 可调参数：Amplitude（振幅）、Frequency（频率）、BaseY（基础高度）

local script = {}

script.Amplitude = 0.5          -- 上下起伏幅度（米）
script.Frequency = 1.5         -- 振荡频率（Hz）
script.BaseY = nil             -- 基础 Y 位置（nil=使用当前 Y）

script._StartY = 0.0            -- 内部：起始 Y 坐标
script._Time = 0.0             -- 内部：累计时间

function script:OnCreate()
    Engine.Info("Floater OnCreate, amp=" .. self.Amplitude .. ", freq=" .. self.Frequency)
    local x, y, z = self.Entity:GetTranslation()
    self._StartY = self.BaseY or y
    self._Time = 0.0
end

function script:OnUpdate(dt)
    self._Time = self._Time + dt
    local x, y, z = self.Entity:GetTranslation()
    local offsetY = self.Amplitude * math.sin(2.0 * math.pi * self.Frequency * self._Time)
    self.Entity:SetTranslation(x, self._StartY + offsetY, z)
end

function script:OnDestroy()
    Engine.Info("Floater OnDestroy")
end

return script
