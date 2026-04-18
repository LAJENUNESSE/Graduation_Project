-- 距离销毁：当实体与指定目标超过 MaxDistance 时自动销毁自身
-- 挂载方式：添加 NativeScriptComponent → Lua 后端 → 选择此脚本
-- 参数 TargetName：目标实体名称；MaxDistance：最大距离（米）

local script = {}

script.TargetName = "Player"     -- 目标实体名称
script.MaxDistance = 20.0       -- 超过此距离自动销毁

script._Distance = 0.0          -- 内部：当前距离

function script:OnCreate()
    Engine.Info("DistanceDestroy OnCreate, target=" .. self.TargetName .. ", maxDist=" .. self.MaxDistance)
end

function script:OnUpdate(dt)
    local target = Scene.FindEntityByName(self.TargetName)
    if not target then
        return
    end

    local dist = self.Entity:DistanceTo(target)
    self._Distance = dist

    if dist > self.MaxDistance then
        Engine.Info("DistanceDestroy: distance " .. dist .. " exceeds " .. self.MaxDistance .. ", destroying self")
        self.Entity:DestroySelf()
    end
end

function script:OnDestroy()
    Engine.Info("DistanceDestroy OnDestroy, final distance=" .. self._Distance)
end

return script
