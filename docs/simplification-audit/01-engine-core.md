# Engine/src 简化审计（01-engine-core）

> 行号基于 2026-08-31 工作区状态（含 FluidSystemGPU/SceneRenderer 未提交改动）。实施前先重新定位。
> 风险标记：🟢 低（可直接做） / 🟡 中（需针对性回归） / 🔴 高（不建议动）

---

# A. 冗余代码合并

## A1 🟢 LuaScriptEngine：5 个事件分发函数复制粘贴

- **位置**：`Engine/src/Script/LuaScriptEngine.cpp:1054-1240`
- **类别**：冗余代码
- **问题**：DispatchCollisionEnter/Stay/Exit、DispatchTriggerEnter/Exit 共 5 个函数结构完全相同（查实例 → 取 table → getfield 回调 → push self → push 参数 → pcall → 报错），仅回调名与"推 CollisionInfo 还是 Entity"不同，每份约 38 行。
- **简化方案**：提取私有辅助 `DispatchEntityCallback`，5 个公开函数各缩为 1-3 行：

```cpp
// LuaScriptEngine.h 私有段新增：
bool DispatchEntityCallback(entt::entity entity, entt::entity otherEntity, const char* callbackName,
                            const glm::vec3* contactPoint, const glm::vec3* contactNormal, float impulse);

// LuaScriptEngine.cpp：
bool LuaScriptEngine::DispatchEntityCallback(entt::entity entity, entt::entity otherEntity,
                                             const char* callbackName, const glm::vec3* contactPoint,
                                             const glm::vec3* contactNormal, float impulse)
{
    if (!m_State)
        return false;
    auto it = m_Instances.find(entity);
    if (it == m_Instances.end())
        return false;

    lua_rawgeti(m_State, LUA_REGISTRYINDEX, it->second.TableRef);
    if (!lua_istable(m_State, -1))
    {
        lua_pop(m_State, 1);
        return false;
    }

    lua_getfield(m_State, -1, callbackName);
    if (!lua_isfunction(m_State, -1))
    {
        lua_pop(m_State, 2);
        return false;
    }

    lua_pushvalue(m_State, -2); // self
    if (contactPoint)           // 碰撞回调带 CollisionInfo；触发器回调只推实体
    {
        Scene* scene           = it->second.ScenePtr;
        PushCollisionInfo(m_State, Entity{otherEntity, scene}, *contactPoint, *contactNormal, impulse);
    }
    else
    {
        Scene* scene = it->second.ScenePtr;
        PushEntity(m_State, Entity{otherEntity, scene});
    }

    if (lua_pcall(m_State, 2, 0, 0) != LUA_OK)
    {
        const char* error = lua_tostring(m_State, -1);
        ENGINE_CORE_ERROR("[Lua] {0} failed: {1}", callbackName, error ? error : "unknown");
        lua_pop(m_State, 1);
        lua_pop(m_State, 1); // table
        return false;
    }
    lua_pop(m_State, 1); // table
    return true;
}

void LuaScriptEngine::DispatchCollisionEnter(entt::entity entity, entt::entity otherEntity,
                                             const glm::vec3& contactPoint, const glm::vec3& contactNormal,
                                             float impulse)
{
    DispatchEntityCallback(entity, otherEntity, "OnCollisionEnter", &contactPoint, &contactNormal, impulse);
}
// CollisionStay 同上；Exit/TriggerEnter/TriggerExit 传 nullptr, nullptr, 0.0f
```

- **前后对比**：5×38 行 → 辅助函数 1 份（约 45 行）+ 5 个 1-3 行转发。
- **预计收益**：约 100 行
- **风险**：🟢 低——纯机械合并；唯一细节是 Exit 版原本推 `Entity` 而非 `CollisionInfo`，方案已用指针判空区分。注意检查 Exit 版原栈平衡与本方案一致。

## A2 🟢 LuaScriptEngine：实体名/Tag 获取三连抄

- **位置**：`LuaScriptEngine.cpp:345-366`（LuaEntityGetName）、`487-507`（LuaSceneGetEntityName）、`523-542`（LuaEntityGetTag）
- **类别**：冗余代码
- **问题**：三段 `HasComponent<TagComponent> ? push tag : push "Entity"` 逐字相同。
- **简化方案**（重构后完整代码）：

```cpp
namespace
{
    // 三个入口共用的"推实体显示名"；调用方先自行校验实体指针
    int PushEntityDisplayName(lua_State* L, const Entity* entityPtr)
    {
        if (entityPtr && *entityPtr && entityPtr->HasComponent<TagComponent>())
            lua_pushstring(L, entityPtr->GetComponent<TagComponent>().Tag.c_str());
        else
            lua_pushstring(L, "Entity");
        return 1;
    }
} // namespace

int LuaEntityGetName(lua_State* L)
{
    auto* entityPtr = static_cast<Entity*>(lua_touserdata(L, 1));
    if (!entityPtr || !(*entityPtr))
    {
        lua_pushstring(L, "");
        return 1;
    }
    return PushEntityDisplayName(L, entityPtr);
}
// LuaSceneGetEntityName / LuaEntityGetTag 同样改为"空指针 push ""，否则委托"
```

- **预计收益**：约 35 行
- **风险**：🟢 低。注意语义保留：**空/无效指针返回空串 `""`，有效但无 Tag 返回 `"Entity"`**——两档不能合并。

## A3 🟢 LuaScriptEngine：4 个日志函数 + 硬编码键值表

- **位置**：`LuaScriptEngine.cpp:25-51`（LuaLogInfo/Warn/Error/Debug）；`936-995`（Key 表 0x57/0x41… 手写 hex）
- **类别**：冗余代码 / 可替换实现
- **问题**：日志 4 连抄仅级别不同；Key 常量表与 `Core/KeyCodes.h` 重复维护（hex 值即 GLFW 码）。
- **简化方案**：

```cpp
namespace
{
    int LuaLogImpl(lua_State* L, spdlog::level::level_enum level)
    {
        const char* message = luaL_checkstring(L, 1);
        ENGINE_CORE_LOG(level, "[Lua] {0}", message ? message : "");
        return 0;
    }
    int LuaLogInfo(lua_State* L)  { return LuaLogImpl(L, spdlog::level::info); }
    int LuaLogWarn(lua_State* L)  { return LuaLogImpl(L, spdlog::level::warn); }
    int LuaLogError(lua_State* L) { return LuaLogImpl(L, spdlog::level::err); }
    int LuaLogDebug(lua_State* L) { return LuaLogImpl(L, spdlog::level::debug); }
}
```

Key/Mouse 注册表改为数据表 + 单循环：

```cpp
struct LuaKeyEntry { const char* LuaName; int KeyCode; };
static constexpr LuaKeyEntry kKeyEntries[] = {
    {"w", Key::W}, {"a", Key::A}, {"s", Key::S}, {"d", Key::D}, /* …与现表逐项对齐… */
};
for (const auto& e : kKeyEntries)
{
    lua_pushinteger(m_State, e.KeyCode);
    lua_setglobal(m_State, e.LuaName);
}
```

- **预计收益**：约 55 行；Key 值从"手写 hex"改为引用 `KeyCodes.h` 常量后消除双处维护（**需逐项比对现表数值与 KeyCodes.h 一致**，这是本条唯一的核对成本）。
- **风险**：🟢 低（若 KeyCodes.h 有名字缺失项则保留对应 hex，加注释指向头文件）。

## A4 🟢 FluidSystemGPU：MeshSDF 调试体回填三连抄

- **位置**：`Engine/src/Renderer/FluidSystemGPU.cpp:654-670`、`814-830`、`1663-1679`
- **类别**：冗余代码
- **问题**：`GPUMeshSDFData → MeshSDFDebugBody` 的 12 字段换算循环逐字出现 3 次（每次约 16 行）。
- **简化方案**（重构后完整代码，匿名命名空间内）：

```cpp
namespace
{
    std::vector<MeshSDFDebugBody> MakeSDFDebugBodies(ShaderStorageBuffer* metaBuffer, uint32_t bodyCount)
    {
        std::vector<MeshSDFDebugBody> bodies;
        if (bodyCount == 0 || !metaBuffer)
            return bodies;

        std::vector<GPUMeshSDFData> metaReadback(bodyCount);
        metaBuffer->GetData(metaReadback.data(), bodyCount * sizeof(GPUMeshSDFData), 0);
        bodies.reserve(bodyCount);
        for (const auto& meta : metaReadback)
        {
            MeshSDFDebugBody dbg{};
            dbg.Center       = glm::vec3(meta.posAndType);
            dbg.Rotation     = glm::eulerAngles(glm::quat_cast(
                glm::mat3(glm::vec3(meta.rotCol0), glm::vec3(meta.rotCol1), glm::vec3(meta.rotCol2))));
            const glm::vec3 invScale    = glm::vec3(meta.invScaleAndBlend);
            const glm::vec3 scale       = glm::max(glm::abs(1.0f / invScale), glm::vec3(1e-4f));
            const glm::vec3 worldExtent = glm::vec3(meta.localExtent) * scale;
            dbg.HalfExtents  = 0.5f * worldExtent;
            dbg.Resolution   = static_cast<uint32_t>(std::max(meta.gridParams.x, 0.0f));
            dbg.VoxelCount   = static_cast<uint32_t>(std::max(meta.gridParams.z, 0.0f));
            dbg.Band         = meta.gridParams.w;
            dbg.Blend        = meta.invScaleAndBlend.w;
            bodies.push_back(dbg);
        }
        return bodies;
    }
} // namespace

// 三处调用点各变一行：
// m_MeshSDFDebugBodies = MakeSDFDebugBodies(m_MeshSDFMetaBuffer.get(), meshSDFCount);
```

- **预计收益**：约 40 行
- **风险**：🟢 低。第三处（1663-1679，Vulkan 路径）需确认其回读前是否有 Vulkan 特有的 barrier 顺序——若 barrier 在函数外已统一处理则无碍；若第三处依赖"调用点即绑定点"则把 Bind 留在调用点、函数内只做 GetData。

## A5 🟢 FluidSystemGPU：PCISPH/WCSPH 两分支的 MeshSDF 缓存上传块重复

- **位置**：`FluidSystemGPU.cpp:626-645` 与 `784-805`；另有 `m_MeshSDFDebugStats.*` 赋值两份（673-679 与 833-839）
- **类别**：冗余代码
- **问题**：哈希比对 → `UploadMeshSDFToBuffers` → 计时 → 缓存三件套在两个 SPH 分支完整重复（各约 30 行）；调试统计赋值又重复一次。
- **简化方案**：提取成员函数：

```cpp
// FluidSystemGPU.h 私有段：
void RefreshMeshSDFCache(const entt::registry& registry, const FluidEmitterComponent& emitter,
                         uint32_t& outBodyCount, uint32_t& outVoxelCount, float& outBuildMs);
void FillMeshSDFDebugStats(const FluidEmitterComponent& emitter, uint32_t bodyCount, uint32_t voxels,
                           float buildMs);

// 实现：
void FluidSystemGPU::RefreshMeshSDFCache(const entt::registry& registry,
                                         const FluidEmitterComponent& emitter, uint32_t& outBodyCount,
                                         uint32_t& outVoxelCount, float& outBuildMs)
{
    outBodyCount = outVoxelCount = 0;
    outBuildMs   = 0.0f;
    if (!emitter.MeshSDFCoupling)
        return;
    InitMeshSDFBuffer();
    const size_t currentHash = ComputeMeshColliderHash(registry);
    if (!m_MeshSDFCacheValid || currentHash != m_MeshSDFCacheHash)
    {
        const auto uploadStart = std::chrono::high_resolution_clock::now();
        m_CachedMeshSDFResult  = UploadMeshSDFToBuffers(
            registry, m_MeshSDFMetaBuffer, m_MeshSDFVoxelBuffer, MAX_MESH_SDF_BODIES,
            static_cast<uint32_t>(std::max(emitter.MeshSDFResolution, 1)), emitter.MeshSDFBand,
            emitter.MeshSDFBlend, RigidBodyUploadFilter::AllColliders);
        const auto uploadEnd = std::chrono::high_resolution_clock::now();
        outBuildMs = std::chrono::duration<float, std::milli>(uploadEnd - uploadStart).count();
        m_MeshSDFCacheHash  = currentHash;
        m_MeshSDFCacheValid = true;
    }
    outBodyCount = m_CachedMeshSDFResult.BodyCount;
    outVoxelCount = m_CachedMeshSDFResult.VoxelCount;
}
```

两个 SPH 分支在 `if (emitter.PCISPHEnabled)` 分派**之前**统一调用一次（两分支本就执行相同逻辑，无顺序差异；分支内保留各自的 Buffer Bind 语句）。
- **预计收益**：约 55 行
- **风险**：🟢 低。实施时核对两分支中 `RigidBodyBuffer->Bind(3)` 的时机差异只在各自分支保留。

## A6 🟢 ParticleSystemGPU：GL/Vulkan 计数器回读清洗块重复

- **位置**：`Engine/src/Renderer/ParticleSystemGPU.cpp:1315-1354`（GL）与 `2056-2096`（Vulkan）
- **类别**：冗余代码
- **问题**：`IsPending&&IsReady → GetData → overflow clamp → 回写 → m_LastAliveCount → CounterDebug` 约 35 行重复，仅"回写方式"（GL `SetData` vs `vkCmdUpdateBuffer`）与 backend 标签不同。
- **简化方案**：提取私有模板成员，回写以可调用对象传入：

```cpp
// ParticleSystemGPU.h 私有段：
template <typename WriteBack>
void ProcessCounterReadback(const char* backendTag, uint32_t emitCount, bool sphEnabled, WriteBack writeBack);

// 实现（.cpp，注意模板放头文件或显式实例化）：
template <typename WriteBack>
void ParticleSystemGPU::ProcessCounterReadback(const char* backendTag, uint32_t emitCount, bool sphEnabled,
                                               WriteBack writeBack)
{
    if (!(m_Readback->IsPending() && m_Readback->IsReady()))
    {
        if (CounterDebugEnabled())
        {
            CounterData probe{};
            m_CounterBuffer->GetData(&probe, sizeof(CounterData));
            LogCounterDebug(backendTag, 0, emitCount, m_LastAliveCount, sphEnabled, probe, false, CounterData{},
                            false);
        }
        return;
    }

    CounterData counters{};
    m_Readback->GetData(&counters, sizeof(CounterData));

    CounterData sanitized = counters;
    bool        corrected = false;
    if (sanitized.deadCount > m_MaxParticles)  { sanitized.deadCount  = m_MaxParticles; corrected = true; }
    if (sanitized.aliveCount > m_MaxParticles) { sanitized.aliveCount = m_MaxParticles; corrected = true; }
    if (corrected)
    {
        ENGINE_WARN("[Particle] Counter overflow detected (dead={0}, alive={1}, max={2}); clamping to safe range.",
                    counters.deadCount, counters.aliveCount, m_MaxParticles);
        writeBack(sanitized); // GL: m_CounterBuffer->SetData(...)；Vulkan: vkCmdUpdateBuffer(...)
    }

    m_LastAliveCount = sanitized.aliveCount;
    if (!m_UseIndirectDraw)
        m_AliveCountForDirectDraw = sanitized.aliveCount;

    if (CounterDebugEnabled())
    {
        CounterData probe{};
        m_CounterBuffer->GetData(&probe, sizeof(CounterData));
        LogCounterDebug(backendTag, 0, emitCount, m_LastAliveCount, sphEnabled, probe, true, counters, corrected);
    }
}
```

- **预计收益**：约 30 行
- **风险**：🟢 低。Vulkan 侧"回写是录制进 cmd 而非立即写"恰由 lambda 封装保住；确认两处的 `LogCounterDebug` 参数（frameSlot=0?）逐一对齐后再替换。

## A7 🟡 "shader → Vulkan compute pipeline" 构建函数 5 处连抄（跨文件）

- **位置**：`Engine/src/Renderer/ParticleSystemGPU.cpp:1536-1564`（buildPipeline）、`1587-1611`（buildOne）；`Engine/src/Renderer/FluidSystemGPU.cpp:1081-1097` 与 `1159-1184`（buildOne）；`Engine/src/Renderer/SpatialHashGrid.cpp:236-266`（buildPipeline）
- **类别**：冗余代码
- **问题**：dynamic_cast → `GetOrCreateShaderModule` → 反射 layout → push constants → `CreateCompute` 的同一约 25 行 lambda 在 3 个文件 5 处出现。
- **简化方案**：在 `Engine/Platform/Vulkan/VulkanPipeline.h` 增加静态工具（实现放 .cpp）：

```cpp
// VulkanPipeline.h
struct VulkanComputePipelineHandle; // 已有类型
static VulkanComputePipelineHandle CreateComputeFromShader(
    VkDevice device, const Ref<Shader>& shader,
    Ref<VulkanDescriptorSetLayout>& outLayout, const char* assertTag);
```

5 处调用收敛为 1 行。
- **预计收益**：约 80 行，且 5 处行为锁成一处
- **风险**：🟡 中——跨文件重构，涉及 Renderer 层对 Platform/Vulkan 头的引用方向（需确认现有 include 方向允许；5 处 push constant 范围/阶段是否完全同构必须逐一比对）。**列为 P2 批次。**

## A8 🟢 StorageBuffer.cpp：5 个工厂函数的 API-switch 五连抄

- **位置**：`Engine/src/Renderer/StorageBuffer.cpp:21-198`
- **类别**：冗余代码
- **问题**：每个 `Create*` 重复约 25 行 `switch(RendererAPI::GetAPI())` 样板（None 断言 / OpenGL / Vulkan + `#ifdef`），只有构造参数不同，共 178 行。
- **简化方案**（重构后完整代码）：

```cpp
namespace
{
    template <typename MakeRef>
    Ref<ShaderStorageBuffer> CreateStorageBuffer(MakeRef makeRef, uint32_t size, uint32_t binding,
                                                 GpuMemCategory memCategory, bool addUploaded)
    {
        Ref<ShaderStorageBuffer> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = makeRef([](auto&&... args) { return CreateRef<OpenGLStorageBuffer>(args...); });
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = makeRef([](auto&&... args) { return CreateRef<VulkanStorageBuffer>(args...); });
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }
        if (!ref)
            return nullptr;

        auto& gmem = GpuMemoryStats::Get();
        gmem.TrackResource(ref, memCategory, size, SsbLabel(size, binding));
        if (addUploaded)
            gmem.AddUploaded(size);
        return ref;
    }
} // namespace

Ref<ShaderStorageBuffer> ShaderStorageBuffer::Create(uint32_t size, uint32_t binding, GpuMemCategory memCategory)
{
    return CreateStorageBuffer(
        [&](auto&& make) { return make(size, binding); }, size, binding, memCategory, false);
}
Ref<ShaderStorageBuffer> ShaderStorageBuffer::Create(const void* data, uint32_t size, uint32_t binding,
                                                     GpuMemCategory memCategory)
{
    return CreateStorageBuffer(
        [&](auto&& make) { return make(data, size, binding); }, size, binding, memCategory, false);
}
// CreateGPUOnly(..., true)、Dynamic 两个重载同理（构造签名见 OpenGLStorageBuffer.h:16-23）
```

> 实现备注：`makeRef` 的 lambda 套 lambda 是为了让 switch 分支与具体后端类型解耦；若嫌绕，可退一步用两个宏 `SSB_GL_ARGS(...)`/`SSB_VK_ARGS(...)` 展开——收益相同。**实施时选择更直白的一种。**

- **预计收益**：约 90 行
- **风险**：🟢 低——模板实例化点行为不变；`TrackResource`/`AddUploaded` 的调用顺序与原 5 处逐一对齐（注意 `CreateGPUOnly` 原文有 `AddUploaded`，其余没有）。

## A9 🟢 Texture.cpp：7 个 Create 同款样板

- **位置**：`Engine/src/Renderer/Texture.cpp:30-220`
- **类别**：冗余代码
- **问题**：同 A8 模式，7 个重载 × 约 27 行 switch 样板，仅构造参数与显存记账公式不同。
- **简化方案**：同 A8 的模板辅助（记账公式作为参数传入）：

```cpp
namespace
{
    template <typename MakeRef>
    Ref<Texture2D> CreateTexture2D(MakeRef makeRef, uint64_t bytes, const std::string& label, bool addUploaded)
    {
        /* switch 样板同 A8，略 */
        auto& gmem = GpuMemoryStats::Get();
        gmem.TrackResource(ref, GpuMemCategory::Texture, bytes, label);
        if (addUploaded) gmem.AddUploaded(bytes);
        return ref;
    }
}
// 各重载：
Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
{
    return CreateTexture2D([&](auto&& make) { return make(width, height); },
                           uint64_t(width) * height * 4, "Tex2D " + std::to_string(width) + "x" + std::to_string(height),
                           false);
}
```

**注意**：7 个重载的记账公式各不相同（路径纹理按 stb 加载字节数、Cubemap ×6、MSAA ×samples 等），必须逐个从原代码抄录，不能统一。
- **预计收益**：约 90 行
- **风险**：🟢 低（记账数值逐个保留原公式）。

## A10 🟢 全屏四边形 VAO 三连抄

- **位置**：`Engine/src/Renderer/PostProcessing.cpp:11-35`、`Engine/src/Renderer/FluidRenderer.cpp:11-35`、`Engine/src/Renderer/SceneRenderer.cpp:105-120`（SSAO 用）
- **类别**：冗余代码
- **问题**：相同的 4 顶点 + 6 索引 + Float2/Float2 layout 三处各约 25 行。
- **简化方案**：新建 `Engine/src/Renderer/FullscreenQuad.{h,cpp}`：

```cpp
// FullscreenQuad.h
#pragma once
#include "Core/Base.h"
namespace Engine
{
    class VertexArray;
    namespace FullscreenQuad
    {
        Ref<VertexArray> Create(); // Position(2)+TexCoord(2)，索引 0,1,2,2,3,0
    }
}

// FullscreenQuad.cpp —— 把 PostProcessing::CreateFullscreenQuad 的顶点/layout 代码原样搬入
```

三处调用点各缩为 1 行；`PostProcessing::m_QuadVAO` 等成员与绑定时机不变。
- **预计收益**：约 50 行
- **风险**：🟢 低。CMake 需把新文件加入 Engine 目标（glob 与显式列表以仓库现状为准）。

## A11 🟡 SceneSerializer：SPH 新旧格式平铺键 20 连抄

- **位置**：`Engine/src/Scene/SceneSerializer.cpp:911-952`
- **类别**：冗余代码
- **问题**：新格式嵌套块（873-910）与旧格式回退块逐字段重复，仅键名前缀与节点不同，约 80 行可压一半。
- **简化方案**：

```cpp
static const std::pair<const char*, const char*> kSphKeys[] = {
    {"Enabled", "SPHEnabled"}, /* …16 组，与现两块逐一对齐… */
};
for (const auto& [newName, oldName] : kSphKeys)
{
    const YAML::Node& src = sphNode ? sphNode[newName] : node[oldName];
    /* 按字段类型赋值——因各字段类型不一，实际实现用一组 if constexpr 或手写 lambda 表 */
}
```

> 因 16 个字段类型各异，"纯循环"需要类型分派表，代码不一定更短；**更稳妥的形态是保留手写但把两块合并为一次遍历**：先取 `YAML::Node src = sphNode ? sphNode : node;`（新旧键名不同的字段先建 `newName→oldName` 别名表补齐 src），然后单份 if 链读一遍。PCISPHIterations 的 clamp 特例保留。
- **预计收益**：约 45 行
- **风险**：🟡 中——**场景文件格式不能变**；键名别名表需与现两块逐一对齐（16 组），漏一项即静默丢配置。列为 P2，实施后必须加载一份新格式 + 一份旧格式场景各验证一次。

## A12 🟡 SceneSerializer：手写反序列化 if 链收敛

- **位置**：`SceneSerializer.cpp:544-588`（RenderSettings）、`798-818`（Terrain 标量）、`833-868`（ParticleEmitter 标量）
- **类别**：冗余代码 / 条件逻辑
- **问题**：约 60 组 `if (node["X"]) obj.X = node["X"].as<T>();` 样板。
- **简化方案**：文件内加静态小工具：

```cpp
namespace
{
    template <typename T>
    void ReadScalar(const YAML::Node& node, const char* key, T& out)
    {
        if (const YAML::Node n = node[key]; n)
            out = n.as<T>();
    }
}
// obj.Enabled = node["Enabled"] ? node["Enabled"].as<bool>() : obj.Enabled;
// → ReadScalar(node, "Enabled", obj.Enabled);
```

`loadSafeTextureHandle` 提升为文件级函数后同样复用。
- **预计收益**：约 70-90 行
- **风险**：🟡 中——纯机械但量大；建议只做 RenderSettings/Terrain/ParticleEmitter 三段，SPH 段配合 A11 一起做。实施后加载既有场景回归（含 RenderSettings 覆盖项）。

---

# B. 死代码删除（grep 已验证零调用点；`#ifdef`/反射/虚函数路径已人工复核）

## B1 🟢 AssetManager::LoadAsync + AsyncLoadQueue 整条异步链

- **位置**：`Engine/src/Asset/AssetManager.cpp:263-310`、`AssetManager.h:19,33,72`、`Engine/src/Asset/AsyncLoadQueue.{h,cpp}`（103 行）
- **类别**：死代码
- **grep 证据**：`LoadAsync` 全仓库仅声明 + 定义两处命中；`SubmitTexture` 唯一调用方是 `LoadAsync` 内部（AssetManager.cpp:303），队列永远收不到任务；`PollResults()`（109 行）与 `s_PendingAsyncLoads`（297 行）永空转。
- **简化方案**：删除 `LoadAsync`、`AsyncLoadQueue.{h,cpp}` 两文件、`s_AsyncQueue`/`s_PendingAsyncLoads` 成员、`Update()` 轮询分支、`Init/Shutdown` 中创建/销毁、CMake 中条目。
- **预计收益**：约 140-150 行 + 每帧一次空轮询
- **风险**：🟢 低——纯死链路。

## B2 🟢 ShaderLibrary 整类

- **位置**：`Engine/src/Renderer/Shader.cpp:59-94`、`Shader.h:37`
- **类别**：死代码
- **grep 证据**：`ShaderLibrary` 排除 Shader.cpp/h 自身后全仓库零命中。
- **简化方案**：删除整个类。
- **预计收益**：约 45 行
- **风险**：🟢 低。

## B3 🟢 SceneRenderer::Render()

- **位置**：`Engine/src/Renderer/SceneRenderer.cpp:486-495`
- **类别**：死代码
- **grep 证据**：Editor 唯一渲染入口 `EditorRenderController.cpp:116-120` 只调 `BeginScene/RenderPipeline/RenderEditorPicking/EndScene`；`->Render()` / `.Render()` 全仓库无命中（其余 Render* 均为前缀匹配的其他函数）。
- **简化方案**：删除该函数及 .h 声明。`Renderer::BeginScene/EndScene` 仍被 GeometryPass 使用，不动。
- **预计收益**：约 10 行
- **风险**：🟢 低。

## B4 🟢 AssetManager::GetPath(handle, type) 两参重载

- **位置**：`Engine/src/Asset/AssetManager.cpp:362-376`、`AssetManager.h:42`
- **类别**：死代码
- **grep 证据**：非模板版零调用；模板版 `GetPath<T>`（.h:45）有调用，**保留**。
- **预计收益**：约 15 行
- **风险**：🟢 低。

## B5 🟢 BulletPhysicsWorld 两个静态转换函数

- **位置**：`Engine/src/Physics/BulletPhysicsWorld.cpp:46-56`
- **类别**：死代码
- **grep 证据**：`EulerToBtQuat` / `BtQuatToEuler` 仅命中定义行；实际代码 316 行用 `btQuaternion(worldRot.x, ...)` 内联转换。
- **简化方案**：删除两函数。
- **预计收益**：12 行
- **风险**：🟢 低。

## B6 🟢 写后不读的成员变量 ×3

- **位置与 grep 证据**：
  - `Engine/src/Renderer/ParticleSystemGPU.h:109` `m_VMwareCompatMode`——全仓库仅 `.h:109` 声明 + `.cpp:691` 赋值，无任何读取（`.cpp:721` 读的是 `GetRequestedInteropBackend()`，不是它）。
  - `ParticleSystemGPU.h:129` `EquivalenceSmoke::MaxParticles`——代码全用 `m_MaxParticles`，该字段从未读写。
  - `Engine/src/Scene/Components.h:165` `RigidBodyComponent::Orientation`——`\bOrientation\b` 排除 Velocity/EulerAngles 变体后全仓库零命中；`ComponentRegistry.cpp:106-117` 未注册；`SceneSerializer.cpp` 无引用。
- **类别**：死代码
- **简化方案**：三者直接删除。
- **预计收益**：约 5 行 + 消除"看似有状态"的误导
- **风险**：🟢 低（`Orientation` 注释写着"持久四元数避免万向节锁"，说明曾有设计意图——删除前与作者确认一次；若未来要用，git 历史可找回）。

## B7 🟢 SceneRenderer ShadowPass 内的空循环

- **位置**：`Engine/src/Renderer/SceneRenderer.cpp:136-145`
- **类别**：死代码 / 控制流
- **问题**：CSM 分支 `for (int i = 0; i < CascadeCount; i++) { m_ShadowSystem.GetSettings(); /* 全是注释 */ }` 逐帧空转（`GetSettings()` 返回引用即丢弃）。
- **简化方案**：删除整个 `if (m_ShadowData.CSMActive) {...}` 块，保留 else 的非 CSM 地形深度渲染。
- **预计收益**：10 行 + 每帧省一次无意义循环
- **风险**：🟢 低（纯 no-op；else 分支行为不变）。

## B8 🟢 ParticleSystemGPU 单值枚举 InteropBackend 机器

- **位置**：`Engine/src/Renderer/ParticleSystemGPU.h:47-55`、`.cpp:53-63, 602-615, 721-727`
- **类别**：死代码 / 过度设计
- **grep 证据**：枚举仅一个值 `CudaGL`；`InteropBackendLabel` 零调用；`GetRequestedInteropBackend()` 恒返回 `CudaGL`；`ActiveInteropBackend` 初始化后从未改写；`.cpp:862, 1399` 的 `ActiveInteropBackend == InteropBackend::CudaGL` 判断恒真。头文件注释已说明 Vulkan-CUDA 互操作整体移除。
- **简化方案**：删枚举、两个静态函数与 `RequestedInteropBackend/ActiveInteropBackend` 字段；两处恒真条件简化为 `!CudaInterop::IsCudaPoisoned()`（保持原短路语义）。
- **预计收益**：约 35 行
- **风险**：🟢 低（恒真条件，无行为变化）。

## B9 🟢 Window::IsRawMouseInputEnabled

- **位置**：`Engine/src/Core/Window.cpp:158, 563-566`、`Window.h:52`
- **类别**：死代码
- **grep 证据**：排除 Window.cpp/h 后零命中（`SetRawMouseInput` 有调用，保留）。
- **简化方案**：删虚函数（基类 + GLFW 实现）。
- **预计收益**：约 6 行
- **风险**：🟢 低。若在意输入接口完整性可保留——待用户拍板（P3）。

---

# F. 执行逻辑简化

## F1 🟡 Scene::Copy / SceneRuntimeCoordinator 手写实体计数循环

- **位置**：`Engine/src/Scene/Scene.cpp:236-248`；`Engine/src/Scene/SceneRuntimeCoordinator.cpp:40-46` 与 `147-153`
- **类别**：可替换实现
- **问题**：三处 `for (view) ++count;` 手数实体。
- **简化方案**：

```cpp
// entt 现代 API：单组件视图可直接问 storage 大小
const size_t srcEntityCount = srcReg.storage<IDComponent>().size();
// 若仓库 entt 版本无 storage()，退化为：
const size_t srcEntityCount = static_cast<size_t>(std::distance(srcReg.view<IDComponent>().begin(),
                                                                 srcReg.view<IDComponent>().end()));
```

- **预计收益**：约 20 行
- **风险**：🟢→🟡：先验证一次 entt 版本的 `storage()` API 可用性；`std::distance` 方案零风险。

## F2 🟢 Lua CallMethod 每帧堆分配 + 字符串比较分派

- **位置**：`Engine/src/Script/LuaScriptEngine.cpp:1398-1403`
- **类别**：控制流 / 数据流
- **问题**：`if (std::string(methodName) == "OnUpdate")` 每次调用构造 std::string 比较；参数个数本可作编译期信息。
- **简化方案**：改为重载 `CallMethod(entity, name, Timestep ts)` / `CallMethod(entity, name)`，内部按重载决定是否 push dt，去掉字符串比较。
- **预计收益**：约 5 行 + 去掉每帧每实体的堆分配
- **风险**：🟢 低（调用方 `SceneRuntimeCoordinator` 的调用点同步改）。

## F3 🟡 FluidSystemGPU GL 路径 m_SurfaceNormalBuffer 同帧三次 Bind

- **位置**：`FluidSystemGPU.cpp:594`、`688`、`761`（同一 binding 8）
- **类别**：控制流（重复操作）
- **问题**：density dispatch 前绑一次，PCISPH/WCSPH 分支内又各绑一次——594 行那次在两分支中都会被覆盖，属无效操作。
- **简化方案**：删 594 行的 `m_SurfaceNormalBuffer->Bind(8)`，保留分支内的。
- **预计收益**：1 行 + 语义更清晰
- **风险**：🟡 中——**需先确认 density dispatch 的 shader 确实不读 binding 8**（binding 8 是 density pass 的写入目标时才安全）。列为 P2，核对 shader `assets/shaders/` 中 density 计算着色器的 binding 声明后再动手。

## F4 🟢 FluidSystemGPU::UpdateVulkan 的 sphEnabled 恒真

- **位置**：`FluidSystemGPU.cpp:1418`
- **类别**：控制流（冗余判断）
- **问题**：1405-1406 行刚做了 `if (!m_SPHInitialized) InitSPH(...)`，到 1418 行 `bool sphEnabled = m_SPHInitialized` 恒为 true，后续 `if (sphEnabled && ...)` 的第一项恒真。
- **简化方案**：删除 `sphEnabled`，1418 行附近直接用 `m_SPHInitialized`；真正的失败出口是 1420-1423 的 `InitSPHVulkanPipelines()` 返回 false，该判断保留原样。
- **预计收益**：约 4 行、少一层假条件
- **风险**：🟢 低（重命名保持 `LogCounterDebug` 等参数含义不变）。

## F5 🟢 PhysicsDebugDraw 死变量

- **位置**：`Engine/src/Physics/PhysicsDebugDraw.cpp:95`
- **类别**：死代码
- **问题**：`glm::vec3 terrainColor = {1.0f, 0.6f, 0.0f};` 声明后从未使用（`DrawTerrainWireframe` 内部 217 行又定义了一份）。
- **简化方案**：删除 95 行。
- **预计收益**：1 行
- **风险**：🟢 无。

---

# 本文档小计

| 类别 | 条目 | 预计行数 |
|---|---|---|
| A 冗余合并 | A1-A12 | 约 660-750 |
| B 死代码 | B1-B9 | 约 280-300 |
| F 逻辑简化 | F1-F5 | 约 30-35 |
| **合计** | 26 条 | **约 970-1085** |

优先动手顺序：B1/B2/B3/B4（整块死删）→ A1/A2/A3（Lua 三连）→ A4/A5/A6（流体/粒子合并）→ A8/A9/A10（工厂/四边形）→ B5/B6/B7/B8/B9、F1/F2/F4/F5（小件）→ A7/A11/A12/F3（中风险，单独立分支）。
