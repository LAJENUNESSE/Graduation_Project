#pragma once
#include "sph_types.cuh"

__device__ inline float poly6(float r2, float h, float poly6Coeff)
{
    float h2 = h * h;
    if (r2 >= h2) return 0.0f;
    float diff = h2 - r2;
    return poly6Coeff * diff * diff * diff;
}

__device__ inline float3 spikyGrad(const float3& diff, float dist, float h, float spikyCoeff)
{
    if (dist <= 0.0f || dist >= h) return make_float3(0.0f, 0.0f, 0.0f);
    float hd = h - dist;
    return spikyCoeff * hd * hd * (diff / dist);
}

__device__ inline float viscLaplacian(float dist, float h, float spikyCoeff)
{
    if (dist >= h) return 0.0f;
    return -spikyCoeff * (h - dist);
}

__device__ inline float C_spline(float r, float h)
{
    float q = r / h;
    float coeff = 32.0f / (PI * h * h * h);
    if (q < 0.5f)
        return coeff * (2.0f * powf(1.0f - q, 3.0f) * powf(q, 3.0f) - 1.0f / 64.0f);
    else if (q < 1.0f)
        return coeff * (powf(1.0f - q, 3.0f) * powf(q, 3.0f) - 1.0f / 64.0f);
    return 0.0f;
}

__device__ inline unsigned int hashCell(int3 cell, int gridSize)
{
    int mx = ((cell.x % gridSize) + gridSize) % gridSize;
    int my = ((cell.y % gridSize) + gridSize) % gridSize;
    int mz = ((cell.z % gridSize) + gridSize) % gridSize;
    return (unsigned int)(mx + my * gridSize + mz * gridSize * gridSize);
}

__device__ inline float3 worldToLocal(const float3& v, const float4& col0, const float4& col1, const float4& col2)
{
    return make_float3(
        col0.x * v.x + col0.y * v.y + col0.z * v.z,
        col1.x * v.x + col1.y * v.y + col1.z * v.z,
        col2.x * v.x + col2.y * v.y + col2.z * v.z
    );
}

__device__ inline float3 localToWorld(const float3& v, const float4& col0, const float4& col1, const float4& col2)
{
    return make_float3(
        v.x * col0.x + v.y * col1.x + v.z * col2.x,
        v.x * col0.y + v.y * col1.y + v.z * col2.y,
        v.x * col0.z + v.y * col1.z + v.z * col2.z
    );
}

__device__ inline float sampleMeshSDF(
    int bodyIndex,
    const float3& localPos,
    const GPUMeshSDFBody* __restrict__ meshSDFBodies,
    const float* __restrict__ meshSDFVoxels)
{
    float resolutionF = meshSDFBodies[bodyIndex].gridParams.x;
    int resolution = (int)resolutionF;
    if (resolution < 1) resolution = 1;
    int voxelOffset = (int)meshSDFBodies[bodyIndex].gridParams.y;
    int voxelCount = (int)meshSDFBodies[bodyIndex].gridParams.z;
    float3 localMin = make_float3(meshSDFBodies[bodyIndex].localMin.x, meshSDFBodies[bodyIndex].localMin.y, meshSDFBodies[bodyIndex].localMin.z);
    float3 localExtent = make_float3(
        fmaxf(meshSDFBodies[bodyIndex].localExtent.x, 1e-5f),
        fmaxf(meshSDFBodies[bodyIndex].localExtent.y, 1e-5f),
        fmaxf(meshSDFBodies[bodyIndex].localExtent.z, 1e-5f)
    );

    float3 uvw = clamp(make_float3(
        (localPos.x - localMin.x) / localExtent.x,
        (localPos.y - localMin.y) / localExtent.y,
        (localPos.z - localMin.z) / localExtent.z
    ), 0.0f, 1.0f);

    float3 g = uvw * (float)(resolution - 1);
    int3 i0 = make_int3((int)floorf(g.x), (int)floorf(g.y), (int)floorf(g.z));
    int3 i1 = make_int3(min(i0.x + 1, resolution - 1), min(i0.y + 1, resolution - 1), min(i0.z + 1, resolution - 1));
    float3 t = make_float3(g.x - floorf(g.x), g.y - floorf(g.y), g.z - floorf(g.z));

    int plane = resolution * resolution;
    int idx000 = i0.z * plane + i0.y * resolution + i0.x;
    int idx100 = i0.z * plane + i0.y * resolution + i1.x;
    int idx010 = i0.z * plane + i1.y * resolution + i0.x;
    int idx110 = i0.z * plane + i1.y * resolution + i1.x;
    int idx001 = i1.z * plane + i0.y * resolution + i0.x;
    int idx101 = i1.z * plane + i0.y * resolution + i1.x;
    int idx011 = i1.z * plane + i1.y * resolution + i0.x;
    int idx111 = i1.z * plane + i1.y * resolution + i1.x;

    int maxIdx = max(voxelCount - 1, 0);
    idx000 = max(0, min(idx000, maxIdx));
    idx100 = max(0, min(idx100, maxIdx));
    idx010 = max(0, min(idx010, maxIdx));
    idx110 = max(0, min(idx110, maxIdx));
    idx001 = max(0, min(idx001, maxIdx));
    idx101 = max(0, min(idx101, maxIdx));
    idx011 = max(0, min(idx011, maxIdx));
    idx111 = max(0, min(idx111, maxIdx));

    float c000 = meshSDFVoxels[voxelOffset + idx000];
    float c100 = meshSDFVoxels[voxelOffset + idx100];
    float c010 = meshSDFVoxels[voxelOffset + idx010];
    float c110 = meshSDFVoxels[voxelOffset + idx110];
    float c001 = meshSDFVoxels[voxelOffset + idx001];
    float c101 = meshSDFVoxels[voxelOffset + idx101];
    float c011 = meshSDFVoxels[voxelOffset + idx011];
    float c111 = meshSDFVoxels[voxelOffset + idx111];

    float c00 = lerp(c000, c100, t.x);
    float c10 = lerp(c010, c110, t.x);
    float c01 = lerp(c001, c101, t.x);
    float c11 = lerp(c011, c111, t.x);
    float c0  = lerp(c00, c10, t.y);
    float c1  = lerp(c01, c11, t.y);
    return lerp(c0, c1, t.z);
}

__device__ inline float3 estimateMeshSDFNormal(
    int bodyIndex,
    const float3& localPos,
    const GPUMeshSDFBody* __restrict__ meshSDFBodies,
    const float* __restrict__ meshSDFVoxels)
{
    float3 localExtent = make_float3(
        fmaxf(meshSDFBodies[bodyIndex].localExtent.x, 1e-5f),
        fmaxf(meshSDFBodies[bodyIndex].localExtent.y, 1e-5f),
        fmaxf(meshSDFBodies[bodyIndex].localExtent.z, 1e-5f)
    );
    float resolution = fmaxf(meshSDFBodies[bodyIndex].gridParams.x, 1.0f);
    float3 e = localExtent / resolution;

    float dx = sampleMeshSDF(bodyIndex, localPos + make_float3(e.x, 0.0f, 0.0f), meshSDFBodies, meshSDFVoxels) -
               sampleMeshSDF(bodyIndex, localPos - make_float3(e.x, 0.0f, 0.0f), meshSDFBodies, meshSDFVoxels);
    float dy = sampleMeshSDF(bodyIndex, localPos + make_float3(0.0f, e.y, 0.0f), meshSDFBodies, meshSDFVoxels) -
               sampleMeshSDF(bodyIndex, localPos - make_float3(0.0f, e.y, 0.0f), meshSDFBodies, meshSDFVoxels);
    float dz = sampleMeshSDF(bodyIndex, localPos + make_float3(0.0f, 0.0f, e.z), meshSDFBodies, meshSDFVoxels) -
               sampleMeshSDF(bodyIndex, localPos - make_float3(0.0f, 0.0f, e.z), meshSDFBodies, meshSDFVoxels);

    float3 n = make_float3(dx, dy, dz);
    return (dot(n, n) > 1e-10f) ? normalize(n) : make_float3(0.0f, 1.0f, 0.0f);
}
