#pragma once

#include "Core/Base.h"

namespace Engine
{

    struct TerrainMeshData;

    // 运行时地形网格数据组件（RAII 管理，不序列化，不反射）
    // 由 TerrainRenderSystem::UpdateTerrainMeshes 懒创建
    struct TerrainRuntimeComponent
    {
        Scope<TerrainMeshData> MeshData;

        TerrainRuntimeComponent() = default;
        TerrainRuntimeComponent(TerrainRuntimeComponent&&) noexcept = default;
        TerrainRuntimeComponent& operator=(TerrainRuntimeComponent&&) noexcept = default;

        // 不可拷贝（Scope = unique_ptr）
        TerrainRuntimeComponent(const TerrainRuntimeComponent&) = delete;
        TerrainRuntimeComponent& operator=(const TerrainRuntimeComponent&) = delete;
    };

} // namespace Engine
