#pragma once
#include <cuda_runtime.h>
#include <cmath>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Vector helper functions for CUDA
__device__ __host__ inline float3 operator+(const float3& a, const float3& b) {
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}
__device__ __host__ inline float3 operator-(const float3& a, const float3& b) {
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}
__device__ __host__ inline float3 operator*(const float3& a, float s) {
    return make_float3(a.x * s, a.y * s, a.z * s);
}
__device__ __host__ inline float3 operator*(float s, const float3& a) {
    return make_float3(a.x * s, a.y * s, a.z * s);
}
__device__ __host__ inline float3 operator/(const float3& a, float s) {
    return make_float3(a.x / s, a.y / s, a.z / s);
}
__device__ __host__ inline float3& operator+=(float3& a, const float3& b) {
    a.x += b.x; a.y += b.y; a.z += b.z; return a;
}
__device__ __host__ inline float3& operator*=(float3& a, float s) {
    a.x *= s; a.y *= s; a.z *= s; return a;
}
__device__ __host__ inline float dot(const float3& a, const float3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
__device__ __host__ inline float3 cross(const float3& a, const float3& b) {
    return make_float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
__device__ __host__ inline float length(const float3& a) {
    return sqrtf(dot(a, a));
}
__device__ __host__ inline float3 normalize(const float3& a) {
    float l = length(a);
    return (l > 1e-7f) ? (a / l) : make_float3(0.0f, 0.0f, 0.0f);
}
__device__ __host__ inline float3 abs(const float3& a) {
    return make_float3(fabsf(a.x), fabsf(a.y), fabsf(a.z));
}
__device__ __host__ inline float3 sign(const float3& a) {
    return make_float3((a.x > 0.0f) ? 1.0f : ((a.x < 0.0f) ? -1.0f : 0.0f),
                       (a.y > 0.0f) ? 1.0f : ((a.y < 0.0f) ? -1.0f : 0.0f),
                       (a.z > 0.0f) ? 1.0f : ((a.z < 0.0f) ? -1.0f : 0.0f));
}
__device__ __host__ inline float3 fmaxf(const float3& a, float s) {
    return make_float3(fmaxf(a.x, s), fmaxf(a.y, s), fmaxf(a.z, s));
}
__device__ __host__ inline float clamp(float v, float lo, float hi) {
    return fminf(fmaxf(v, lo), hi);
}
__device__ __host__ inline float3 clamp(const float3& v, float lo, float hi) {
    return make_float3(clamp(v.x, lo, hi), clamp(v.y, lo, hi), clamp(v.z, lo, hi));
}
__device__ __host__ inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}
__device__ __host__ inline int3 make_int3_floor(const float3& v) {
    return make_int3((int)floorf(v.x), (int)floorf(v.y), (int)floorf(v.z));
}

// Data structures matching shader layouts
struct GPUParticle
{
    float4 posAndLife;       // xyz=position, w=remainingLife
    float4 velAndMaxLife;    // xyz=velocity, w=maxLife
    float4 startColor;
    float4 endColor;
    float4 params;           // x=sizeStart, y=sizeEnd, z=density(SPH), w=pressure(SPH)
};

struct PCISPHData
{
    float4 predictedPosAndPressure;  // xyz=x*, w=P
    float4 predictedVelAndDensity;   // xyz=v*, w=rho*
    float4 nonPressureAccel;         // xyz=a_np, w=unused
};

struct GPURigidBody
{
    float4 posAndType;    // xyz=center, w=0(box)/1(sphere)
    float4 rotCol0;
    float4 rotCol1;
    float4 rotCol2;
    float4 halfExtents;   // box: xyz=halfExtents; sphere: x=radius
    float4 linearVel;
    float4 angularVel;
};

struct GPUMeshSDFBody
{
    float4 posAndType;
    float4 rotCol0;
    float4 rotCol1;
    float4 rotCol2;
    float4 invScaleAndBlend;
    float4 localMin;
    float4 localExtent;
    float4 gridParams;
};

struct SPHParams
{
    float4 u_GravityAndSmoothingRadius;     // xyz=Gravity, w=SmoothingRadius
    float4 u_MassDensityGasViscosity;       // x=ParticleMass, y=RestDensity, z=GasConstant, w=Viscosity
    float4 u_GridParams;                    // x=GridSize, y=CellSize, z=Poly6Coeff, w=SpikyCoeff
    float4 u_BoundaryParams;                // x=BoundaryStiffness, y=BoundaryDamping, z=WarmupTime, w=SurfaceTension
    float4 u_SDFCounts;                     // x=RigidBodyCount, y=MeshSDFCount, z=MeshSDFVoxelCount, w=PCISPHDelta
    
    unsigned int u_AliveCount;
    float        u_DeltaTime;
    unsigned int u_UsePredictedPos;
    unsigned int _pad0;
};
