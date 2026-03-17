#include "engpch.h"
#include "Terrain/TerrainMeshGenerator.h"
#include "Asset/PathUtils.h"
#include "Core/Log.h"
#include "Renderer/Buffer.h"

#include <glm/glm.hpp>
#include <stb_image/stb_image.h>

#include <algorithm>
#include <cmath>
namespace Engine
{

    TerrainMeshData
    TerrainMeshGenerator::Generate(const std::string& heightmapPath, float size, float heightScale, int lodLevels)
    {
        TerrainMeshData data;

        int               w = 0, h = 0, channels = 0;
        const std::string resolvedPath = PathUtils::ResolvePathString(heightmapPath);
        unsigned char*    pixels       = stbi_load(resolvedPath.c_str(), &w, &h, &channels, 1);

        if (!pixels)
        {
            ENGINE_CORE_WARN("TerrainMeshGenerator: 高度图加载失败 '{}', 生成平面地形", heightmapPath);
            // 生成平面高度数据 64x64
            w                    = 64;
            h                    = 64;
            data.HeightmapWidth  = w;
            data.HeightmapHeight = h;
            data.HeightData.resize(w * h, 0.0f);
            data.MinHeight = 0.0f;
            data.MaxHeight = 0.0f;
        }
        else
        {
            data.HeightmapWidth  = w;
            data.HeightmapHeight = h;
            data.HeightData.resize(w * h);

            float minH = 1.0f;
            float maxH = 0.0f;

            for (int i = 0; i < w * h; ++i)
            {
                float normalized   = static_cast<float>(pixels[i]) / 255.0f;
                data.HeightData[i] = normalized;
                minH               = std::min(minH, normalized);
                maxH               = std::max(maxH, normalized);
            }

            data.MinHeight = minH;
            data.MaxHeight = maxH;

            stbi_image_free(pixels);
        }

        // 生成 LOD 级别: LOD0=(w-1), LOD1=(w-1)/2, LOD2=(w-1)/4, ...
        int baseRes = std::max(w - 1, 1);
        lodLevels   = std::min(lodLevels, 4); // 最多 4 级

        for (int lod = 0; lod < lodLevels; ++lod)
        {
            int gridRes = baseRes >> lod; // baseRes / 2^lod
            if (gridRes < 2)
                gridRes = 2; // 最少 2x2 网格

            data.LODs.push_back(BuildLOD(data.HeightData, w, h, gridRes, size, heightScale));
        }

        ENGINE_CORE_INFO("TerrainMeshGenerator: 生成完毕, 高度图 {}x{}, {} 级 LOD", w, h, (int)data.LODs.size());
        return data;
    }

    // 双线性采样高度图
    static float SampleHeight(const std::vector<float>& heightData, int hmWidth, int hmHeight, float fx, float fy)
    {
        // 钳制到 [0, width-1] 和 [0, height-1]
        fx = std::max(0.0f, std::min(fx, static_cast<float>(hmWidth - 1)));
        fy = std::max(0.0f, std::min(fy, static_cast<float>(hmHeight - 1)));

        int x0 = static_cast<int>(fx);
        int y0 = static_cast<int>(fy);
        int x1 = std::min(x0 + 1, hmWidth - 1);
        int y1 = std::min(y0 + 1, hmHeight - 1);

        float sx = fx - static_cast<float>(x0);
        float sy = fy - static_cast<float>(y0);

        float h00 = heightData[y0 * hmWidth + x0];
        float h10 = heightData[y0 * hmWidth + x1];
        float h01 = heightData[y1 * hmWidth + x0];
        float h11 = heightData[y1 * hmWidth + x1];

        // 双线性插值
        float h0 = h00 + sx * (h10 - h00);
        float h1 = h01 + sx * (h11 - h01);
        return h0 + sy * (h1 - h0);
    }

    TerrainLODMesh TerrainMeshGenerator::BuildLOD(
        const std::vector<float>& heightData, int hmWidth, int hmHeight, int gridRes, float size, float heightScale)
    {
        TerrainLODMesh lod;
        lod.GridResolution = gridRes;

        int vertCountX = gridRes + 1;
        int vertCountZ = gridRes + 1;
        int totalVerts = vertCountX * vertCountZ;

        float cellSize = size / static_cast<float>(gridRes);
        float halfSize = size * 0.5f;

        // 11 floats per vertex: Pos(3) + Normal(3) + UV(2) + Tangent(3)
        std::vector<float> vertices(totalVerts * 11);

        // 辅助: 采样网格点 (i,j) 对应的高度
        auto getHeight = [&](int i, int j) -> float
        {
            float fx = static_cast<float>(i) / static_cast<float>(gridRes) * static_cast<float>(hmWidth - 1);
            float fy = static_cast<float>(j) / static_cast<float>(gridRes) * static_cast<float>(hmHeight - 1);
            return SampleHeight(heightData, hmWidth, hmHeight, fx, fy);
        };

        for (int j = 0; j < vertCountZ; ++j)
        {
            for (int i = 0; i < vertCountX; ++i)
            {
                int idx    = j * vertCountX + i;
                int offset = idx * 11;

                float h = getHeight(i, j);

                // 位置
                float posX = static_cast<float>(i) / static_cast<float>(gridRes) * size - halfSize;
                float posY = h * heightScale;
                float posZ = static_cast<float>(j) / static_cast<float>(gridRes) * size - halfSize;

                vertices[offset + 0] = posX;
                vertices[offset + 1] = posY;
                vertices[offset + 2] = posZ;

                // 法线: 中心差分, 边界用单侧差分
                float hL, hR, hD, hU;
                float dxScale, dzScale;

                if (i == 0)
                {
                    hL      = h;
                    hR      = getHeight(i + 1, j);
                    dxScale = cellSize;
                }
                else if (i == gridRes)
                {
                    hL      = getHeight(i - 1, j);
                    hR      = h;
                    dxScale = cellSize;
                }
                else
                {
                    hL      = getHeight(i - 1, j);
                    hR      = getHeight(i + 1, j);
                    dxScale = 2.0f * cellSize;
                }

                if (j == 0)
                {
                    hD      = h;
                    hU      = getHeight(i, j + 1);
                    dzScale = cellSize;
                }
                else if (j == gridRes)
                {
                    hD      = getHeight(i, j - 1);
                    hU      = h;
                    dzScale = cellSize;
                }
                else
                {
                    hD      = getHeight(i, j - 1);
                    hU      = getHeight(i, j + 1);
                    dzScale = 2.0f * cellSize;
                }

                // normal = normalize( (hL - hR) * heightScale, dxScale/dzScale_combined, (hD - hU) * heightScale )
                // 更精确: 用交叉法线方法
                glm::vec3 normal = glm::normalize(glm::vec3((hL - hR) * heightScale,
                                                            dxScale, // 近似: 使用水平步长作为 y 分量
                                                            (hD - hU) * heightScale));

                vertices[offset + 3] = normal.x;
                vertices[offset + 4] = normal.y;
                vertices[offset + 5] = normal.z;

                // UV: 全局 UV 用于 Splatmap
                float u              = static_cast<float>(i) / static_cast<float>(gridRes);
                float v              = static_cast<float>(j) / static_cast<float>(gridRes);
                vertices[offset + 6] = u;
                vertices[offset + 7] = v;

                // 切线: 沿 X 方向
                glm::vec3 tangent = glm::normalize(glm::vec3(dxScale, (hR - hL) * heightScale, 0.0f));

                vertices[offset + 8]  = tangent.x;
                vertices[offset + 9]  = tangent.y;
                vertices[offset + 10] = tangent.z;
            }
        }

        // 索引: 每格两个三角形
        int                   totalQuads = gridRes * gridRes;
        std::vector<uint32_t> indices(totalQuads * 6);

        int idxOffset = 0;
        for (int j = 0; j < gridRes; ++j)
        {
            for (int i = 0; i < gridRes; ++i)
            {
                uint32_t topLeft     = static_cast<uint32_t>(j * vertCountX + i);
                uint32_t topRight    = topLeft + 1;
                uint32_t bottomLeft  = static_cast<uint32_t>((j + 1) * vertCountX + i);
                uint32_t bottomRight = bottomLeft + 1;

                // 三角形 1
                indices[idxOffset++] = topLeft;
                indices[idxOffset++] = bottomLeft;
                indices[idxOffset++] = topRight;

                // 三角形 2
                indices[idxOffset++] = topRight;
                indices[idxOffset++] = bottomLeft;
                indices[idxOffset++] = bottomRight;
            }
        }

        lod.IndexCount = static_cast<uint32_t>(indices.size());

        // 创建 VAO
        lod.VAO = VertexArray::Create();

        auto vbo = VertexBuffer::Create(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(float)));
        vbo->SetLayout({{ShaderDataType::Float3, "a_Position"},
                        {ShaderDataType::Float3, "a_Normal"},
                        {ShaderDataType::Float2, "a_TexCoords"},
                        {ShaderDataType::Float3, "a_Tangent"}});
        lod.VAO->AddVertexBuffer(vbo);

        auto ibo = IndexBuffer::Create(indices.data(), static_cast<uint32_t>(indices.size()));
        lod.VAO->SetIndexBuffer(ibo);

        return lod;
    }

} // namespace Engine
