#pragma once

#include "Core/Base.h"
#include "Renderer/VertexArray.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Engine
{

    struct TerrainLODMesh
    {
        Ref<VertexArray> VAO;
        uint32_t IndexCount = 0;
        int GridResolution = 0;
    };

    struct TerrainMeshData
    {
        std::vector<TerrainLODMesh> LODs;
        std::vector<float> HeightData; // 归一化 [0,1]
        int HeightmapWidth = 0;
        int HeightmapHeight = 0;
        float MinHeight = 0.0f;
        float MaxHeight = 1.0f;
    };

    class TerrainMeshGenerator
    {
    public:
        static TerrainMeshData Generate(const std::string& heightmapPath, float size, float heightScale,
                                        int lodLevels = 3);

    private:
        static TerrainLODMesh BuildLOD(const std::vector<float>& heightData, int hmWidth, int hmHeight, int gridRes,
                                       float size, float heightScale);
    };

} // namespace Engine
