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
    -- 注意：当前 Lua API 不支持按名称查找实体
    -- 需要 Scene:FindEntityByName API 才能实现此功能
    -- 目前此脚本作为占位实现
    local x, y, z = self.Entity:GetTranslation()
    self._Distance = math.sqrt(x * x + y * y + z * z)
end

function script:OnDestroy()
    Engine.Info("DistanceDestroy OnDestroy, final distance=" .. self._Distance)
end

return script
