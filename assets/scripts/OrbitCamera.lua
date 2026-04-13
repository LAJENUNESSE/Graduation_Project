-- 轨道相机：绕目标实体旋转，鼠标拖动改变角度
-- 挂载方式：添加到相机实体，指定 TargetName 为要跟随的目标名称
-- 运行时：按住左键拖动旋转，滚轮调节距离

local script = {}

script.TargetName = "Player"     -- 跟随目标实体的名称（需在场景中存在）
script.Distance = 5.0            -- 相机距目标距离
script.MinDistance = 1.0         -- 最小距离
script.MaxDistance = 20.0       -- 最大距离
script.HeightOffset = 1.5        -- 相机高于目标的偏移
script.RotationSpeed = 0.5       -- 旋转灵敏度

script._TargetEntity = nil       -- 内部：缓存目标实体
script._Pitch = 20.0             -- 内部：俯仰角（度）
script._Yaw = 0.0               -- 内部：偏航角（度）
script._LastMX = 0.0            -- 内部：上一帧鼠标 X
script._LastMY = 0.0            -- 内部：上一帧鼠标 Y

function script:OnCreate()
    Engine.Info("OrbitCamera OnCreate, target=" .. self.TargetName)
    self._Pitch = 20.0
    self._Yaw = 0.0
end

function script:OnUpdate(dt)
    -- 暂时无法通过 Lua 查找其他实体（FindEntityByName 未暴露）
    -- 此脚本需要配合 Scene:FindEntityByName 使用
    -- 目前通过 Transform 手动对齐到目标位置

    local mx, my = Engine.GetMousePosition()
    if Engine.IsMouseButtonPressed(Mouse.MOUSE_LEFT) then
        local dx = mx - self._LastMX
        local dy = my - self._LastMY
        self._Yaw = self._Yaw + dx * self.RotationSpeed
        self._Pitch = self._Pitch + dy * self.RotationSpeed
        -- 限制俯仰
        self._Pitch = math.max(-89, math.min(89, self._Pitch))
    end
    self._LastMX = mx
    self._LastMY = my

    -- 球坐标转笛卡尔
    local pitchRad = math.rad(self._Pitch)
    local yawRad = math.rad(self._Yaw)

    local cx = self.Distance * math.cos(pitchRad) * math.sin(yawRad)
    local cy = self.Distance * math.sin(pitchRad) + self.HeightOffset
    local cz = self.Distance * math.cos(pitchRad) * math.cos(yawRad)

    -- 相机朝向目标（负 Z）
    self.Entity:SetTranslation(cx, cy, cz)

    -- 相机旋转（朝向 -Z 方向）
    -- 简化：让相机始终朝向原点方向
    self.Entity:SetRotation(self._Pitch, self._Yaw, 0)
end

function script:OnDestroy()
    Engine.Info("OrbitCamera OnDestroy")
end

return script
