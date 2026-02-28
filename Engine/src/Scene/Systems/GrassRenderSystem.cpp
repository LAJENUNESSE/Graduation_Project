#include "engpch.h"
#include "Scene/Systems/GrassRenderSystem.h"
#include "Scene/Components.h"
#include "Terrain/TerrainMeshGenerator.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer.h"
#include "Renderer/RendererAPI.h"
#include "Renderer/RendererCapabilities.h"
#include "Renderer/EditorCamera.h"
#include "Asset/AssetManager.h"
#include "Core/Log.h"

#include <glad/gl.h>
#include <cstring>
#include <algorithm>

namespace Engine
{

    namespace
    {
        bool ContainsToken(const char* str, const char* token)
        {
            return str && token && std::strstr(str, token) != nullptr;
        }

        // Counter buffer layout: 1 x uint32 = 4 bytes
        struct GrassCounterData
        {
            uint32_t grassCount;
        };

        // DrawArraysIndirectCommand: 4 x uint32 = 16 bytes
        struct IndirectDrawCommand
        {
            uint32_t count;
            uint32_t instanceCount;
            uint32_t first;
            uint32_t baseInstance;
        };

        static constexpr uint32_t MAX_GRASS_BLADES = 500000;
    }

    void GrassRenderSystem::Init()
    {
        if (!RendererCapabilities::Get().SupportsComputeShaders)
        {
            ENGINE_WARN("[Grass] Compute shaders not supported, grass system disabled.");
            return;
        }

        m_PlacementShader  = Shader::Create("assets/shaders/grass_placement.glsl");
        m_RenderArgsShader = Shader::Create("assets/shaders/grass_render_args.glsl");
        m_BillboardShader  = Shader::Create("assets/shaders/grass_billboard.glsl");

        auto whiteHandle = AssetManager::Load<Texture2D>("builtin:white");
        m_WhiteTexture = AssetManager::GetRef<Texture2D>(whiteHandle);

        m_EmptyVAO = VertexArray::Create();

        // VMware 兼容检测
        const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        bool vmwareDriver = ContainsToken(vendor, "VMware") || ContainsToken(renderer, "SVGA3D");

        const char* forceDirect = std::getenv("ENGINE_GRASS_DIRECT_DRAW");
        bool envForceDirect = forceDirect && forceDirect[0] == '1';

        m_UseIndirectDraw = !(vmwareDriver || envForceDirect);
        if (!m_UseIndirectDraw)
        {
            ENGINE_WARN("[Grass] Using direct instanced draw fallback (VMware compatibility mode).");
        }
    }

    void GrassRenderSystem::Shutdown()
    {
        m_Instances.clear();
        m_Cache.clear();
    }

    void GrassRenderSystem::UpdateGrassData(entt::registry& reg, float totalTime)
    {
        if (!RendererCapabilities::Get().SupportsComputeShaders)
            return;

        auto view = reg.view<TransformComponent, TerrainComponent>();
        for (auto entity : view)
        {
            auto& tc = view.get<TerrainComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            uint32_t eid = static_cast<uint32_t>(entity);

            if (!tc.GrassEnabled)
            {
                // 草地禁用时清理
                m_Instances.erase(eid);
                m_Cache.erase(eid);
                continue;
            }

            // 脏检测
            auto& cache = m_Cache[eid];
            bool dirty = cache.GrassEnabled != tc.GrassEnabled
                || cache.GrassDensity != tc.GrassDensity
                || cache.GrassHeight != tc.GrassHeight
                || cache.GrassWidth != tc.GrassWidth
                || cache.GrassWindStrength != tc.GrassWindStrength
                || cache.TerrainSize != tc.TerrainSize
                || cache.HeightScale != tc.HeightScale
                || cache.HeightmapPath != tc.HeightmapPath
                || cache.GrassTexture != tc.GrassTexture;

            if (dirty)
            {
                RebuildGrass(eid, tc, transform);

                cache.GrassEnabled = tc.GrassEnabled;
                cache.GrassDensity = tc.GrassDensity;
                cache.GrassHeight = tc.GrassHeight;
                cache.GrassWidth = tc.GrassWidth;
                cache.GrassWindStrength = tc.GrassWindStrength;
                cache.TerrainSize = tc.TerrainSize;
                cache.HeightScale = tc.HeightScale;
                cache.HeightmapPath = tc.HeightmapPath;
                cache.GrassTexture = tc.GrassTexture;
            }
        }
    }

    void GrassRenderSystem::RebuildGrass(uint32_t eid, TerrainComponent& tc,
                                          const TransformComponent& transform)
    {
        auto* meshData = static_cast<TerrainMeshData*>(tc.RuntimeMeshData);
        if (!meshData || meshData->HeightData.empty())
        {
            ENGINE_WARN("[Grass] Entity {} has no terrain mesh data, skipping grass rebuild.", eid);
            return;
        }

        uint32_t maxGrass = static_cast<uint32_t>(
            std::min(static_cast<double>(tc.TerrainSize * tc.TerrainSize * tc.GrassDensity),
                     static_cast<double>(MAX_GRASS_BLADES)));

        if (maxGrass == 0)
        {
            m_Instances.erase(eid);
            return;
        }

        auto& inst = m_Instances[eid];

        // 草叶 SSBO: 80 bytes/blade (5 x vec4)
        uint32_t grassBufSize = maxGrass * 80;
        inst.GrassBuffer = ShaderStorageBuffer::CreateGPUOnly(grassBufSize, 0);

        // 高度图 SSBO
        uint32_t heightBufSize = static_cast<uint32_t>(meshData->HeightData.size() * sizeof(float));
        inst.HeightBuffer = ShaderStorageBuffer::CreateGPUOnly(
            meshData->HeightData.data(), heightBufSize, 1);

        // Counter SSBO (dynamic, 需要 fallback 回读)
        GrassCounterData counter{0};
        inst.CounterBuffer = ShaderStorageBuffer::Create(&counter, sizeof(GrassCounterData), 3);

        // IndirectArgs SSBO
        IndirectDrawCommand cmd{6, 0, 0, 0};
        inst.IndirectArgs = ShaderStorageBuffer::CreateGPUOnly(&cmd, sizeof(IndirectDrawCommand), 4);

        // Dispatch placement compute
        inst.GrassBuffer->Bind(0);
        inst.HeightBuffer->Bind(1);
        inst.CounterBuffer->Bind(3);

        m_PlacementShader->Bind();
        m_PlacementShader->SetInt("u_MaxGrass", static_cast<int>(maxGrass));
        m_PlacementShader->SetInt("u_HeightmapWidth", meshData->HeightmapWidth);
        m_PlacementShader->SetInt("u_HeightmapHeight", meshData->HeightmapHeight);
        m_PlacementShader->SetFloat("u_TerrainSize", tc.TerrainSize);
        m_PlacementShader->SetFloat("u_HeightScale", tc.HeightScale);
        m_PlacementShader->SetFloat("u_GrassHeight", tc.GrassHeight);
        m_PlacementShader->SetFloat("u_GrassWidth", tc.GrassWidth);
        m_PlacementShader->SetFloat("u_GrassWindStrength", tc.GrassWindStrength);

        uint32_t groups = (maxGrass + 63) / 64;
        RenderCommand::DispatchCompute(groups);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage);

        // Dispatch render_args compute
        inst.IndirectArgs->Bind(4);
        m_RenderArgsShader->Bind();
        RenderCommand::DispatchCompute(1);
        RenderCommand::MemoryBarrier(BarrierBit::ShaderStorage | BarrierBit::Command);

        // Fallback: 回读 grassCount 用于 instanced draw
        if (!m_UseIndirectDraw)
        {
            GrassCounterData readback{0};
            inst.CounterBuffer->GetData(&readback, sizeof(GrassCounterData));
            inst.GrassCount = readback.grassCount;
        }
    }

    void GrassRenderSystem::Render(entt::registry& reg, const EditorCamera& camera,
                                    const LightEnvironment& lights, const ShadowData& shadow,
                                    const ShadowSettings& shadowSettings, float totalTime)
    {
        if (!RendererCapabilities::Get().SupportsComputeShaders)
            return;
        if (m_Instances.empty())
            return;

        Renderer::BeginScene(camera.GetViewProjection());

        m_BillboardShader->Bind();
        m_BillboardShader->SetMat4("u_ViewProjection", camera.GetViewProjection());
        m_BillboardShader->SetFloat("u_Time", totalTime);

        // 光照
        LightSystem::UploadToShader(m_BillboardShader, lights);

        // 阴影
        m_BillboardShader->SetMat4("u_LightSpaceMatrix", shadow.LightSpaceMatrix);
        bool shadowActive = shadowSettings.Enabled && shadow.HasValidShadowCaster;
        m_BillboardShader->SetInt("u_ShadowEnabled", shadowActive ? 1 : 0);
        m_BillboardShader->SetFloat("u_ShadowBias", shadowSettings.Bias);
        RenderCommand::BindTextureUnit(1, shadow.ShadowMapTextureID);
        m_BillboardShader->SetInt("u_ShadowMap", 1);

        // 禁用面剔除（草叶两面可见）
        RenderCommand::SetCullFace(false);

        auto view = reg.view<TransformComponent, TerrainComponent>();
        for (auto entity : view)
        {
            auto& tc = view.get<TerrainComponent>(entity);
            if (!tc.GrassEnabled) continue;

            uint32_t eid = static_cast<uint32_t>(entity);
            auto it = m_Instances.find(eid);
            if (it == m_Instances.end()) continue;

            auto& inst = it->second;
            auto& transform = view.get<TransformComponent>(entity);

            m_BillboardShader->SetMat4("u_Transform", transform.GetTransform());
            m_BillboardShader->SetInt("u_EntityID", static_cast<int>(eid));

            // 绑定草地纹理 (unit 2)
            Texture2D* grassTex = AssetManager::Get<Texture2D>(tc.GrassTexture);
            if (grassTex)
                grassTex->Bind(2);
            else
                m_WhiteTexture->Bind(2);
            m_BillboardShader->SetInt("u_GrassTexture", 2);

            // 绑定 SSBO
            inst.GrassBuffer->Bind(0);

            m_EmptyVAO->Bind();
            if (m_UseIndirectDraw)
                RenderCommand::DrawArraysIndirect(inst.IndirectArgs->GetRendererID());
            else
                RenderCommand::DrawArraysInstanced(6, inst.GrassCount);
        }

        // 恢复面剔除
        RenderCommand::SetCullFace(true);

        Renderer::EndScene();
    }

} // namespace Engine
