#ifndef SPH_COMMON_HLSLI
#define SPH_COMMON_HLSLI

// =============================================================================
// SPH Common Data Structures & Utility Functions for DirectCompute / HLSL
// =============================================================================

#define PI 3.14159265359f

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

cbuffer SPHConstants : register(b0)
{
    float4 u_GravityAndSmoothingRadius;     // xyz=Gravity, w=SmoothingRadius
    float4 u_MassDensityGasViscosity;       // x=ParticleMass, y=RestDensity, z=GasConstant, w=Viscosity
    float4 u_GridParams;                    // x=GridSize, y=CellSize, z=Poly6Coeff, w=SpikyCoeff
    float4 u_BoundaryParams;                // x=BoundaryStiffness, y=BoundaryDamping, z=WarmupTime, w=SurfaceTension
    float4 u_SDFCounts;                     // x=RigidBodyCount, y=MeshSDFCount, z=MeshSDFVoxelCount, w=PCISPHDelta
    
    uint   u_AliveCount;
    float  u_DeltaTime;
    uint   u_UsePredictedPos;
    uint   _pad0;
};

#define u_Gravity            u_GravityAndSmoothingRadius.xyz
#define u_SmoothingRadius    u_GravityAndSmoothingRadius.w
#define u_ParticleMass       u_MassDensityGasViscosity.x
#define u_RestDensity        u_MassDensityGasViscosity.y
#define u_GasConstant        u_MassDensityGasViscosity.z
#define u_Viscosity          u_MassDensityGasViscosity.w
#define u_GridSize           int(u_GridParams.x)
#define u_CellSize           u_GridParams.y
#define u_Poly6Coeff         u_GridParams.z
#define u_SpikyCoeff         u_GridParams.w
#define u_BoundaryStiffness  u_BoundaryParams.x
#define u_BoundaryDamping    u_BoundaryParams.y
#define u_WarmupTime         u_BoundaryParams.z
#define u_SurfaceTension     u_BoundaryParams.w
#define u_RigidBodyCount     int(u_SDFCounts.x)
#define u_MeshSDFCount       int(u_SDFCounts.y)
#define u_MeshSDFVoxelCount  int(u_SDFCounts.z)
#define u_PCISPHDelta        u_SDFCounts.w

// Poly6 kernel: W(r, h) = u_Poly6Coeff * (h^2 - r^2)^3
inline float poly6(float r2, float h)
{
    float h2 = h * h;
    if (r2 >= h2) return 0.0f;
    float diff = h2 - r2;
    return u_Poly6Coeff * diff * diff * diff;
}

// Spiky kernel gradient: grad W_spiky = u_SpikyCoeff * (h - |r|)^2 * (r / |r|)
inline float3 spikyGrad(float3 diff, float dist, float h)
{
    if (dist <= 0.0f || dist >= h) return float3(0.0f, 0.0f, 0.0f);
    float hd = h - dist;
    return u_SpikyCoeff * hd * hd * (diff / dist);
}

// Viscosity kernel Laplacian: laplacian W_visc = -u_SpikyCoeff * (h - |r|)
inline float viscLaplacian(float dist, float h)
{
    if (dist >= h) return 0.0f;
    return -u_SpikyCoeff * (h - dist);
}

// Akinci C_spline surface tension kernel
inline float C_spline(float r, float h)
{
    float q = r / h;
    float coeff = 32.0f / (PI * h * h * h);
    if (q < 0.5f)
        return coeff * (2.0f * pow(1.0f - q, 3.0f) * pow(q, 3.0f) - 1.0f / 64.0f);
    else if (q < 1.0f)
        return coeff * (pow(1.0f - q, 3.0f) * pow(q, 3.0f) - 1.0f / 64.0f);
    return 0.0f;
}

// Spatial hash: maps 3D cell to 1D index
inline uint hashCell(int3 cell, int gridSize)
{
    int3 m = ((cell % gridSize) + gridSize) % gridSize;
    return (uint)(m.x + m.y * gridSize + m.z * gridSize * gridSize);
}

// Transform world vector to local using rigid body column vectors
inline float3 worldToLocal(float3 v, float4 col0, float4 col1, float4 col2)
{
    return float3(dot(col0.xyz, v), dot(col1.xyz, v), dot(col2.xyz, v));
}

// Transform local vector to world using rigid body column vectors
inline float3 localToWorld(float3 v, float4 col0, float4 col1, float4 col2)
{
    return v.x * col0.xyz + v.y * col1.xyz + v.z * col2.xyz;
}

#endif // SPH_COMMON_HLSLI
