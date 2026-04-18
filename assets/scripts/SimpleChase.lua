-- 简单追逐 AI：每帧朝向目标移动，超出停止距离后停止
-- 挂载方式：添加到敌人实体，指定 TargetName 和 Speed

local script = {}

script.TargetName = "Player"     -- 目标实体名称
script.Speed = 2.0               -- 移动速度（单位/秒）
script.StopDistance = 1.0        -- 停止距离（米）
script.StartDistance = 15.0      -- 开始追击的距离（米）

script._Distance = 0.0           -- 内部：当前距离
script._TargetEntity = nil       -- 内部：目标实体缓存

function script:OnCreate()
    Engine.Info("SimpleChase OnCreate, target=" .. self.TargetName .. ", speed=" .. self.Speed)
    self._TargetEntity = Scene.FindEntityByName(self.TargetName)
    if not self._TargetEntity then
        Engine.Warn("SimpleChase: target '" .. self.TargetName .. "' not found")
    end
end

function script:OnUpdate(dt)
    -- 查找目标实体
    self._TargetEntity = Scene.FindEntityByName(self.TargetName)
    if not self._TargetEntity then
        return
    end

    local dist = self.Entity:DistanceTo(self._TargetEntity)
    self._Distance = dist

    if dist > self.StopDistance and dist < self.StartDistance then
        local ex, ey, ez = self.Entity:GetTranslation()
        local tx, ty, tz = self._TargetEntity:GetTranslation()
        local dx, dy, dz = tx - ex, ty - ey, tz - ez
        local len = math.sqrt(dx * dx + dy * dy + dz * dz)
        if len > 0.001 then
            local move = self.Speed * dt / len
            self.Entity:Translate(dx * move, dy * move, dz * move)
        end
    end
end

function script:OnDestroy()
    Engine.Info("SimpleChase OnDestroy, final distance=" .. self._Distance)
end

return script move = self.Speed * dt / len
    --             self.Entity:Translate(dx * move, dy * move, dz * move)
    --         end
    --     end
    -- end

    local x, y, z = self.Entity:GetTranslation()
    self._Distance = math.sqrt(x * x + y * y + z * z)
end

function script:OnDestroy()
    Engine.Info("SimpleChase OnDestroy, final distance=" .. self._Distance)
end

return script
