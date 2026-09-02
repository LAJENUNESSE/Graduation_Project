#include "sph_types.cuh"
#include "sph_math.cuh"

// 1. SPH Density Kernel
__global__ void sph_density_kernel(
    GPUParticle* __restrict__ particles,
    float4* __restrict__ surfaceNormals,
    const unsigned int* __restrict__ aliveIndices,
    const unsigned int* __restrict__ cellStart,
    const unsigned int* __restrict__ cellCount,
    const unsigned int* __restrict__ sortedIndices,
    SPHParams params)
{
    unsigned int gid = blockIdx.x * blockDim.x + threadIdx.x;
    if (gid >= params.u_AliveCount) return;

    unsigned int myParticleIdx = aliveIndices[gid];
    float3 posI = make_float3(particles[myParticleIdx].posAndLife.x, particles[myParticleIdx].posAndLife.y, particles[myParticleIdx].posAndLife.z);
    float h = params.u_GravityAndSmoothingRadius.w;
    float particleMass = params.u_MassDensityGasViscosity.x;
    float poly6Coeff = params.u_GridParams.z;
    float spikyCoeff = params.u_GridParams.w;
    float surfaceTension = params.u_BoundaryParams.w;
    float cellSize = params.u_GridParams.y;
    int gridSize = (int)params.u_GridParams.x;

    float density = particleMass * poly6(0.0f, h, poly6Coeff);
    float3 surfaceNormal = make_float3(0.0f, 0.0f, 0.0f);
    int3 myCell = make_int3((int)floorf(posI.x / cellSize), (int)floorf(posI.y / cellSize), (int)floorf(posI.z / cellSize));

    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        int3 neighborCell = make_int3(myCell.x + dx, myCell.y + dy, myCell.z + dz);
        unsigned int cellIdx = hashCell(neighborCell, gridSize);
        unsigned int cCount = cellCount[cellIdx];
        if (cCount == 0u) continue;
        unsigned int cEnd = min(cellStart[cellIdx], params.u_AliveCount);
        unsigned int cBegin = (cCount > cEnd) ? 0u : (cEnd - cCount);

        for (unsigned int s = cBegin; s < cEnd; s++)
        {
            unsigned int neighborAliveIdx = sortedIndices[s];
            if (neighborAliveIdx >= params.u_AliveCount) continue;
            unsigned int neighborParticleIdx = aliveIndices[neighborAliveIdx];
            if (neighborParticleIdx == myParticleIdx) continue;

            float3 posJ = make_float3(particles[neighborParticleIdx].posAndLife.x, particles[neighborParticleIdx].posAndLife.y, particles[neighborParticleIdx].posAndLife.z);
            float3 diff = posI - posJ;
            float r2 = dot(diff, diff);
            density += particleMass * poly6(r2, h, poly6Coeff);

            if (surfaceTension > 0.0f)
            {
                float dist = sqrtf(r2);
                float densityJ = fmaxf(particles[neighborParticleIdx].params.z, 0.0001f);
                surfaceNormal += (particleMass / densityJ) * spikyGrad(diff, dist, h, spikyCoeff);
            }
        }
    }

    float restDensity = params.u_MassDensityGasViscosity.y;
    float gasConstant = params.u_MassDensityGasViscosity.z;
    particles[myParticleIdx].params.z = density;
    particles[myParticleIdx].params.w = fmaxf(0.0f, gasConstant * (density - restDensity));

    surfaceNormals[gid] = (surfaceTension > 0.0f)
        ? make_float4(h * surfaceNormal.x, h * surfaceNormal.y, h * surfaceNormal.z, 0.0f)
        : make_float4(0.0f, 0.0f, 0.0f, 0.0f);
}

// 2. SPH Force Kernel
__global__ void sph_force_kernel(
    GPUParticle* __restrict__ particles,
    const unsigned int* __restrict__ aliveIndices,
    const unsigned int* __restrict__ cellStart,
    const unsigned int* __restrict__ cellCount,
    const unsigned int* __restrict__ sortedIndices,
    const float4* __restrict__ surfaceNormals,
    const GPURigidBody* __restrict__ rigidBodies,
    const GPUMeshSDFBody* __restrict__ meshSDFBodies,
    const float* __restrict__ meshSDFVoxels,
    SPHParams params)
{
    unsigned int gid = blockIdx.x * blockDim.x + threadIdx.x;
    if (gid >= params.u_AliveCount) return;

    unsigned int myParticleIdx = aliveIndices[gid];
    float3 posI = make_float3(particles[myParticleIdx].posAndLife.x, particles[myParticleIdx].posAndLife.y, particles[myParticleIdx].posAndLife.z);
    float3 velI = make_float3(particles[myParticleIdx].velAndMaxLife.x, particles[myParticleIdx].velAndMaxLife.y, particles[myParticleIdx].velAndMaxLife.z);
    float densityI = fmaxf(particles[myParticleIdx].params.z, 0.0001f);
    float pressureI = particles[myParticleIdx].params.w;
    float h = params.u_GravityAndSmoothingRadius.w;
    float particleMass = params.u_MassDensityGasViscosity.x;
    float spikyCoeff = params.u_GridParams.w;
    float viscosity = params.u_MassDensityGasViscosity.w;
    float surfaceTension = params.u_BoundaryParams.w;
    float cellSize = params.u_GridParams.y;
    int gridSize = (int)params.u_GridParams.x;

    float3 fPressure = make_float3(0.0f, 0.0f, 0.0f);
    float3 fViscosity = make_float3(0.0f, 0.0f, 0.0f);
    float3 fSurfaceTension = make_float3(0.0f, 0.0f, 0.0f);
    float3 normalI = make_float3(surfaceNormals[gid].x, surfaceNormals[gid].y, surfaceNormals[gid].z);
    int3 myCell = make_int3((int)floorf(posI.x / cellSize), (int)floorf(posI.y / cellSize), (int)floorf(posI.z / cellSize));

    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        int3 neighborCell = make_int3(myCell.x + dx, myCell.y + dy, myCell.z + dz);
        unsigned int cellIdx = hashCell(neighborCell, gridSize);
        unsigned int cCount = cellCount[cellIdx];
        if (cCount == 0u) continue;
        unsigned int cEnd = min(cellStart[cellIdx], params.u_AliveCount);
        unsigned int cBegin = (cCount > cEnd) ? 0u : (cEnd - cCount);

        for (unsigned int s = cBegin; s < cEnd; s++)
        {
            unsigned int neighborAliveIdx = sortedIndices[s];
            if (neighborAliveIdx >= params.u_AliveCount) continue;
            unsigned int neighborParticleIdx = aliveIndices[neighborAliveIdx];
            if (neighborParticleIdx == myParticleIdx) continue;

            float3 posJ = make_float3(particles[neighborParticleIdx].posAndLife.x, particles[neighborParticleIdx].posAndLife.y, particles[neighborParticleIdx].posAndLife.z);
            float3 velJ = make_float3(particles[neighborParticleIdx].velAndMaxLife.x, particles[neighborParticleIdx].velAndMaxLife.y, particles[neighborParticleIdx].velAndMaxLife.z);
            float densityJ = fmaxf(particles[neighborParticleIdx].params.z, 0.0001f);
            float pressureJ = particles[neighborParticleIdx].params.w;
            float3 diff = posI - posJ;
            float dist = length(diff);

            fPressure += -particleMass * (pressureI + pressureJ) / (2.0f * densityJ) * spikyGrad(diff, dist, h, spikyCoeff);
            fViscosity += viscosity * particleMass * (velJ - velI) / densityJ * viscLaplacian(dist, h, spikyCoeff);

            if (surfaceTension > 0.0f && dist > 0.001f)
            {
                fSurfaceTension += -surfaceTension * particleMass * C_spline(dist, h) * (diff / dist);
                float3 normalJ = make_float3(surfaceNormals[neighborAliveIdx].x, surfaceNormals[neighborAliveIdx].y, surfaceNormals[neighborAliveIdx].z);
                fSurfaceTension += -surfaceTension * particleMass * (normalI - normalJ);
            }
        }
    }

    // Rigid body SDF
    float3 fBoundary = make_float3(0.0f, 0.0f, 0.0f);
    int rbCount = (int)params.u_SDFCounts.x;
    for (int rb = 0; rb < rbCount; rb++)
    {
        float3 rbPos = make_float3(rigidBodies[rb].posAndType.x, rigidBodies[rb].posAndType.y, rigidBodies[rb].posAndType.z);
        float rbType = rigidBodies[rb].posAndType.w;
        float3 localPos = worldToLocal(posI - rbPos, rigidBodies[rb].rotCol0, rigidBodies[rb].rotCol1, rigidBodies[rb].rotCol2);
        float sdf;
        float3 localNormal;

        if (rbType < 0.5f) // Box
        {
            float3 he = make_float3(rigidBodies[rb].halfExtents.x, rigidBodies[rb].halfExtents.y, rigidBodies[rb].halfExtents.z);
            float3 d = abs(localPos) - he;
            sdf = length(fmaxf(d, 0.0f)) + fminf(fmaxf(d.x, fmaxf(d.y, d.z)), 0.0f);
            float3 s = sign(localPos);
            float3 outside = fmaxf(d, 0.0f);
            float outsideLen = length(outside);
            if (outsideLen > 1e-6f) localNormal = s * (outside / outsideLen);
            else if (d.x > d.y && d.x > d.z) localNormal = make_float3(s.x, 0.0f, 0.0f);
            else if (d.y > d.z) localNormal = make_float3(0.0f, s.y, 0.0f);
            else localNormal = make_float3(0.0f, 0.0f, s.z);
        }
        else // Sphere
        {
            sdf = length(localPos) - rigidBodies[rb].halfExtents.x;
            localNormal = (length(localPos) > 0.001f) ? normalize(localPos) : make_float3(0.0f, 1.0f, 0.0f);
        }

        if (sdf < h)
        {
            float3 worldNormal = localToWorld(localNormal, rigidBodies[rb].rotCol0, rigidBodies[rb].rotCol1, rigidBodies[rb].rotCol2);
            float3 rbVel = make_float3(rigidBodies[rb].linearVel.x, rigidBodies[rb].linearVel.y, rigidBodies[rb].linearVel.z)
                         + cross(make_float3(rigidBodies[rb].angularVel.x, rigidBodies[rb].angularVel.y, rigidBodies[rb].angularVel.z), posI - rbPos);
            float3 velRel = velI - rbVel;

            if (sdf < 0.0f)
            {
                posI += (-sdf + 0.001f) * worldNormal;
                particles[myParticleIdx].posAndLife.x = posI.x;
                particles[myParticleIdx].posAndLife.y = posI.y;
                particles[myParticleIdx].posAndLife.z = posI.z;
                float vn = dot(velRel, worldNormal);
                if (vn < 0.0f)
                {
                    velI -= vn * worldNormal;
                    particles[myParticleIdx].velAndMaxLife.x = velI.x;
                    particles[myParticleIdx].velAndMaxLife.y = velI.y;
                    particles[myParticleIdx].velAndMaxLife.z = velI.z;
                }
            }

            float penetration = fmaxf(0.0f, h - sdf);
            fBoundary += params.u_BoundaryParams.x * penetration * worldNormal
                       - params.u_BoundaryParams.y * dot(velRel, worldNormal) * worldNormal;
        }
    }

    // Mesh SDF
    int meshCount = (int)params.u_SDFCounts.y;
    int voxelCount = (int)params.u_SDFCounts.z;
    if (voxelCount > 0)
    {
        for (int mb = 0; mb < meshCount; mb++)
        {
            float3 mbPos = make_float3(meshSDFBodies[mb].posAndType.x, meshSDFBodies[mb].posAndType.y, meshSDFBodies[mb].posAndType.z);
            float3 localPos = worldToLocal(posI - mbPos, meshSDFBodies[mb].rotCol0, meshSDFBodies[mb].rotCol1, meshSDFBodies[mb].rotCol2);
            float sdf = sampleMeshSDF(mb, localPos, meshSDFBodies, meshSDFVoxels);
            float3 localNormal = estimateMeshSDFNormal(mb, localPos, meshSDFBodies, meshSDFVoxels);

            if (sdf < h)
            {
                float3 worldNormal = localToWorld(localNormal, meshSDFBodies[mb].rotCol0, meshSDFBodies[mb].rotCol1, meshSDFBodies[mb].rotCol2);
                float blend = clamp(meshSDFBodies[mb].invScaleAndBlend.w, 0.0f, 1.0f);
                float3 velRel = velI;

                if (sdf < 0.0f)
                {
                    posI += (-sdf + 0.001f) * worldNormal;
                    particles[myParticleIdx].posAndLife.x = posI.x;
                    particles[myParticleIdx].posAndLife.y = posI.y;
                    particles[myParticleIdx].posAndLife.z = posI.z;
                    float vn = dot(velRel, worldNormal);
                    if (vn < 0.0f)
                    {
                        velI -= vn * worldNormal;
                        particles[myParticleIdx].velAndMaxLife.x = velI.x;
                        particles[myParticleIdx].velAndMaxLife.y = velI.y;
                        particles[myParticleIdx].velAndMaxLife.z = velI.z;
                    }
                }

                float penetration = fmaxf(0.0f, h - sdf);
                float3 f = params.u_BoundaryParams.x * penetration * worldNormal
                         - params.u_BoundaryParams.y * dot(velRel, worldNormal) * worldNormal;
                fBoundary += f * blend;
            }
        }
    }

    float warmupTime = params.u_BoundaryParams.z;
    float age = particles[myParticleIdx].velAndMaxLife.w - particles[myParticleIdx].posAndLife.w;
    float warmup = (warmupTime > 0.0f) ? clamp(age / warmupTime, 0.0f, 1.0f) : 1.0f;

    float3 sphAccel = (fPressure * warmup + fViscosity + fSurfaceTension) / densityI + fBoundary;
    float accelMag = length(sphAccel);
    if (accelMag > 500.0f) sphAccel *= 500.0f / accelMag;

    particles[myParticleIdx].velAndMaxLife.x += sphAccel.x * params.u_DeltaTime;
    particles[myParticleIdx].velAndMaxLife.y += sphAccel.y * params.u_DeltaTime;
    particles[myParticleIdx].velAndMaxLife.z += sphAccel.z * params.u_DeltaTime;
}

// 3. PCISPH Init Kernel
__global__ void sph_pcisph_init_kernel(
    const GPUParticle* __restrict__ particles,
    PCISPHData* __restrict__ pcisphData,
    const unsigned int* __restrict__ aliveIndices,
    const unsigned int* __restrict__ cellStart,
    const unsigned int* __restrict__ cellCount,
    const unsigned int* __restrict__ sortedIndices,
    const float4* __restrict__ surfaceNormals,
    SPHParams params)
{
    unsigned int gid = blockIdx.x * blockDim.x + threadIdx.x;
    if (gid >= params.u_AliveCount) return;

    unsigned int myParticleIdx = aliveIndices[gid];
    float3 posI = make_float3(particles[myParticleIdx].posAndLife.x, particles[myParticleIdx].posAndLife.y, particles[myParticleIdx].posAndLife.z);
    float3 velI = make_float3(particles[myParticleIdx].velAndMaxLife.x, particles[myParticleIdx].velAndMaxLife.y, particles[myParticleIdx].velAndMaxLife.z);
    float densityI = fmaxf(particles[myParticleIdx].params.z, 0.0001f);
    float h = params.u_GravityAndSmoothingRadius.w;
    float particleMass = params.u_MassDensityGasViscosity.x;
    float spikyCoeff = params.u_GridParams.w;
    float viscosity = params.u_MassDensityGasViscosity.w;
    float surfaceTension = params.u_BoundaryParams.w;
    float cellSize = params.u_GridParams.y;
    int gridSize = (int)params.u_GridParams.x;
    float3 gravity = make_float3(params.u_GravityAndSmoothingRadius.x, params.u_GravityAndSmoothingRadius.y, params.u_GravityAndSmoothingRadius.z);

    float3 fViscosity = make_float3(0.0f, 0.0f, 0.0f);
    float3 fSurfTension = make_float3(0.0f, 0.0f, 0.0f);
    float3 normalI = make_float3(surfaceNormals[gid].x, surfaceNormals[gid].y, surfaceNormals[gid].z);
    int3 myCell = make_int3((int)floorf(posI.x / cellSize), (int)floorf(posI.y / cellSize), (int)floorf(posI.z / cellSize));

    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        int3 neighborCell = make_int3(myCell.x + dx, myCell.y + dy, myCell.z + dz);
        unsigned int cellIdx = hashCell(neighborCell, gridSize);
        unsigned int cCount = cellCount[cellIdx];
        if (cCount == 0u) continue;
        unsigned int cEnd = min(cellStart[cellIdx], params.u_AliveCount);
        unsigned int cBegin = (cCount > cEnd) ? 0u : (cEnd - cCount);

        for (unsigned int s = cBegin; s < cEnd; s++)
        {
            unsigned int neighborAliveIdx = sortedIndices[s];
            if (neighborAliveIdx >= params.u_AliveCount) continue;
            unsigned int neighborParticleIdx = aliveIndices[neighborAliveIdx];
            if (neighborParticleIdx == myParticleIdx) continue;

            float3 posJ = make_float3(particles[neighborParticleIdx].posAndLife.x, particles[neighborParticleIdx].posAndLife.y, particles[neighborParticleIdx].posAndLife.z);
            float3 velJ = make_float3(particles[neighborParticleIdx].velAndMaxLife.x, particles[neighborParticleIdx].velAndMaxLife.y, particles[neighborParticleIdx].velAndMaxLife.z);
            float densityJ = fmaxf(particles[neighborParticleIdx].params.z, 0.0001f);
            float3 diff = posI - posJ;
            float dist = length(diff);

            fViscosity += viscosity * particleMass * (velJ - velI) / densityJ * viscLaplacian(dist, h, spikyCoeff);

            if (surfaceTension > 0.0f && dist > 0.001f)
            {
                fSurfTension += -surfaceTension * particleMass * C_spline(dist, h) * (diff / dist);
                float3 normalJ = make_float3(surfaceNormals[neighborAliveIdx].x, surfaceNormals[neighborAliveIdx].y, surfaceNormals[neighborAliveIdx].z);
                fSurfTension += -surfaceTension * particleMass * (normalI - normalJ);
            }
        }
    }

    float3 a_np = (fViscosity + fSurfTension) / densityI + gravity;
    float accelMag = length(a_np);
    if (accelMag > 500.0f) a_np *= 500.0f / accelMag;

    pcisphData[gid].nonPressureAccel = make_float4(a_np.x, a_np.y, a_np.z, 0.0f);
    pcisphData[gid].predictedVelAndDensity = make_float4(
        velI.x + a_np.x * params.u_DeltaTime,
        velI.y + a_np.y * params.u_DeltaTime,
        velI.z + a_np.z * params.u_DeltaTime,
        0.0f
    );
    pcisphData[gid].predictedPosAndPressure.w = 0.0f;
}

// 4. PCISPH Predict Kernel
__global__ void sph_pcisph_predict_kernel(
    const GPUParticle* __restrict__ particles,
    PCISPHData* __restrict__ pcisphData,
    const unsigned int* __restrict__ aliveIndices,
    SPHParams params)
{
    unsigned int gid = blockIdx.x * blockDim.x + threadIdx.x;
    if (gid >= params.u_AliveCount) return;

    unsigned int myParticleIdx = aliveIndices[gid];
    float3 pos = make_float3(particles[myParticleIdx].posAndLife.x, particles[myParticleIdx].posAndLife.y, particles[myParticleIdx].posAndLife.z);
    float3 vStar = make_float3(pcisphData[gid].predictedVelAndDensity.x, pcisphData[gid].predictedVelAndDensity.y, pcisphData[gid].predictedVelAndDensity.z);

    pcisphData[gid].predictedPosAndPressure.x = pos.x + params.u_DeltaTime * vStar.x;
    pcisphData[gid].predictedPosAndPressure.y = pos.y + params.u_DeltaTime * vStar.y;
    pcisphData[gid].predictedPosAndPressure.z = pos.z + params.u_DeltaTime * vStar.z;
}

// 5. PCISPH Density Kernel
__global__ void sph_pcisph_density_kernel(
    const GPUParticle* __restrict__ particles,
    PCISPHData* __restrict__ pcisphData,
    const unsigned int* __restrict__ aliveIndices,
    const unsigned int* __restrict__ cellStart,
    const unsigned int* __restrict__ cellCount,
    const unsigned int* __restrict__ sortedIndices,
    SPHParams params)
{
    unsigned int gid = blockIdx.x * blockDim.x + threadIdx.x;
    if (gid >= params.u_AliveCount) return;

    unsigned int myParticleIdx = aliveIndices[gid];
    float3 predPosI = make_float3(pcisphData[gid].predictedPosAndPressure.x, pcisphData[gid].predictedPosAndPressure.y, pcisphData[gid].predictedPosAndPressure.z);
    float h = params.u_GravityAndSmoothingRadius.w;
    float particleMass = params.u_MassDensityGasViscosity.x;
    float poly6Coeff = params.u_GridParams.z;
    float cellSize = params.u_GridParams.y;
    int gridSize = (int)params.u_GridParams.x;

    float density = particleMass * poly6(0.0f, h, poly6Coeff);
    float3 lookupPosI = (params.u_UsePredictedPos != 0)
        ? predPosI
        : make_float3(particles[myParticleIdx].posAndLife.x, particles[myParticleIdx].posAndLife.y, particles[myParticleIdx].posAndLife.z);
    int3 myCell = make_int3((int)floorf(lookupPosI.x / cellSize), (int)floorf(lookupPosI.y / cellSize), (int)floorf(lookupPosI.z / cellSize));

    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        int3 neighborCell = make_int3(myCell.x + dx, myCell.y + dy, myCell.z + dz);
        unsigned int cellIdx = hashCell(neighborCell, gridSize);
        unsigned int cCount = cellCount[cellIdx];
        if (cCount == 0u) continue;
        unsigned int cEnd = min(cellStart[cellIdx], params.u_AliveCount);
        unsigned int cBegin = (cCount > cEnd) ? 0u : (cEnd - cCount);

        for (unsigned int s = cBegin; s < cEnd; s++)
        {
            unsigned int neighborAliveIdx = sortedIndices[s];
            if (neighborAliveIdx >= params.u_AliveCount) continue;
            unsigned int neighborParticleIdx = aliveIndices[neighborAliveIdx];
            if (neighborParticleIdx == myParticleIdx) continue;

            float3 predPosJ = make_float3(pcisphData[neighborAliveIdx].predictedPosAndPressure.x, pcisphData[neighborAliveIdx].predictedPosAndPressure.y, pcisphData[neighborAliveIdx].predictedPosAndPressure.z);
            float3 diff = predPosI - predPosJ;
            density += particleMass * poly6(dot(diff, diff), h, poly6Coeff);
        }
    }

    float restDensity = params.u_MassDensityGasViscosity.y;
    float delta = params.u_SDFCounts.w;

    pcisphData[gid].predictedVelAndDensity.w = density;
    pcisphData[gid].predictedPosAndPressure.w += delta * fmaxf(0.0f, density - restDensity);
    pcisphData[gid].predictedPosAndPressure.w = fminf(pcisphData[gid].predictedPosAndPressure.w, 50000.0f);
}

// 6. PCISPH Force Kernel
__global__ void sph_pcisph_force_kernel(
    const GPUParticle* __restrict__ particles,
    PCISPHData* __restrict__ pcisphData,
    const unsigned int* __restrict__ aliveIndices,
    const GPURigidBody* __restrict__ rigidBodies,
    const GPUMeshSDFBody* __restrict__ meshSDFBodies,
    const float* __restrict__ meshSDFVoxels,
    const unsigned int* __restrict__ cellStart,
    const unsigned int* __restrict__ cellCount,
    const unsigned int* __restrict__ sortedIndices,
    SPHParams params)
{
    unsigned int gid = blockIdx.x * blockDim.x + threadIdx.x;
    if (gid >= params.u_AliveCount) return;

    unsigned int myParticleIdx = aliveIndices[gid];
    float3 posI = (params.u_UsePredictedPos != 0)
        ? make_float3(pcisphData[gid].predictedPosAndPressure.x, pcisphData[gid].predictedPosAndPressure.y, pcisphData[gid].predictedPosAndPressure.z)
        : make_float3(particles[myParticleIdx].posAndLife.x, particles[myParticleIdx].posAndLife.y, particles[myParticleIdx].posAndLife.z);
    float pressureI = pcisphData[gid].predictedPosAndPressure.w;
    float densityI = fmaxf(pcisphData[gid].predictedVelAndDensity.w, 0.0001f);
    float h = params.u_GravityAndSmoothingRadius.w;
    float particleMass = params.u_MassDensityGasViscosity.x;
    float spikyCoeff = params.u_GridParams.w;
    float cellSize = params.u_GridParams.y;
    int gridSize = (int)params.u_GridParams.x;

    float3 fPressure = make_float3(0.0f, 0.0f, 0.0f);
    int3 myCell = make_int3((int)floorf(posI.x / cellSize), (int)floorf(posI.y / cellSize), (int)floorf(posI.z / cellSize));

    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        int3 neighborCell = make_int3(myCell.x + dx, myCell.y + dy, myCell.z + dz);
        unsigned int cellIdx = hashCell(neighborCell, gridSize);
        unsigned int cCount = cellCount[cellIdx];
        if (cCount == 0u) continue;
        unsigned int cEnd = min(cellStart[cellIdx], params.u_AliveCount);
        unsigned int cBegin = (cCount > cEnd) ? 0u : (cEnd - cCount);

        for (unsigned int s = cBegin; s < cEnd; s++)
        {
            unsigned int neighborAliveIdx = sortedIndices[s];
            if (neighborAliveIdx >= params.u_AliveCount) continue;
            unsigned int neighborParticleIdx = aliveIndices[neighborAliveIdx];
            if (neighborParticleIdx == myParticleIdx) continue;

            float3 posJ = (params.u_UsePredictedPos != 0)
                ? make_float3(pcisphData[neighborAliveIdx].predictedPosAndPressure.x, pcisphData[neighborAliveIdx].predictedPosAndPressure.y, pcisphData[neighborAliveIdx].predictedPosAndPressure.z)
                : make_float3(particles[neighborParticleIdx].posAndLife.x, particles[neighborParticleIdx].posAndLife.y, particles[neighborParticleIdx].posAndLife.z);
            float pressureJ = pcisphData[neighborAliveIdx].predictedPosAndPressure.w;
            float densityJ  = fmaxf(pcisphData[neighborAliveIdx].predictedVelAndDensity.w, 0.0001f);
            float3 diff = posI - posJ;
            fPressure += -particleMass * (pressureI / (densityI * densityI) + pressureJ / (densityJ * densityJ)) * spikyGrad(diff, length(diff), h, spikyCoeff);
        }
    }

    // Rigid body SDF boundary
    float3 fBoundary = make_float3(0.0f, 0.0f, 0.0f);
    int rbCount = (int)params.u_SDFCounts.x;
    for (int rb = 0; rb < rbCount; rb++)
    {
        float3 rbPos = make_float3(rigidBodies[rb].posAndType.x, rigidBodies[rb].posAndType.y, rigidBodies[rb].posAndType.z);
        float rbType = rigidBodies[rb].posAndType.w;
        float3 localPos = worldToLocal(posI - rbPos, rigidBodies[rb].rotCol0, rigidBodies[rb].rotCol1, rigidBodies[rb].rotCol2);
        float sdf;
        float3 localNormal;

        if (rbType < 0.5f)
        {
            float3 he = make_float3(rigidBodies[rb].halfExtents.x, rigidBodies[rb].halfExtents.y, rigidBodies[rb].halfExtents.z);
            float3 d = abs(localPos) - he;
            sdf = length(fmaxf(d, 0.0f)) + fminf(fmaxf(d.x, fmaxf(d.y, d.z)), 0.0f);
            float3 s = sign(localPos);
            float3 outside = fmaxf(d, 0.0f);
            float outsideLen = length(outside);
            if (outsideLen > 1e-6f) localNormal = s * (outside / outsideLen);
            else if (d.x > d.y && d.x > d.z) localNormal = make_float3(s.x, 0.0f, 0.0f);
            else if (d.y > d.z) localNormal = make_float3(0.0f, s.y, 0.0f);
            else localNormal = make_float3(0.0f, 0.0f, s.z);
        }
        else
        {
            sdf = length(localPos) - rigidBodies[rb].halfExtents.x;
            localNormal = (length(localPos) > 0.001f) ? normalize(localPos) : make_float3(0.0f, 1.0f, 0.0f);
        }

        if (sdf < h)
        {
            float3 worldNormal = localToWorld(localNormal, rigidBodies[rb].rotCol0, rigidBodies[rb].rotCol1, rigidBodies[rb].rotCol2);
            float3 rbVel = make_float3(rigidBodies[rb].linearVel.x, rigidBodies[rb].linearVel.y, rigidBodies[rb].linearVel.z)
                         + cross(make_float3(rigidBodies[rb].angularVel.x, rigidBodies[rb].angularVel.y, rigidBodies[rb].angularVel.z), posI - rbPos);
            float3 velRel = make_float3(pcisphData[gid].predictedVelAndDensity.x, pcisphData[gid].predictedVelAndDensity.y, pcisphData[gid].predictedVelAndDensity.z) - rbVel;
            float penetration = fmaxf(0.0f, h - sdf);
            fBoundary += params.u_BoundaryParams.x * penetration * worldNormal
                       - params.u_BoundaryParams.y * dot(velRel, worldNormal) * worldNormal;
        }
    }

    // Mesh SDF boundary
    int meshCount = (int)params.u_SDFCounts.y;
    int voxelCount = (int)params.u_SDFCounts.z;
    if (voxelCount > 0)
    {
        for (int mb = 0; mb < meshCount; mb++)
        {
            float3 mbPos = make_float3(meshSDFBodies[mb].posAndType.x, meshSDFBodies[mb].posAndType.y, meshSDFBodies[mb].posAndType.z);
            float3 localPos = worldToLocal(posI - mbPos, meshSDFBodies[mb].rotCol0, meshSDFBodies[mb].rotCol1, meshSDFBodies[mb].rotCol2);
            float sdf = sampleMeshSDF(mb, localPos, meshSDFBodies, meshSDFVoxels);
            float3 localNormal = estimateMeshSDFNormal(mb, localPos, meshSDFBodies, meshSDFVoxels);

            if (sdf < h)
            {
                float3 worldNormal = localToWorld(localNormal, meshSDFBodies[mb].rotCol0, meshSDFBodies[mb].rotCol1, meshSDFBodies[mb].rotCol2);
                float blend = clamp(meshSDFBodies[mb].invScaleAndBlend.w, 0.0f, 1.0f);
                float3 velRel = make_float3(pcisphData[gid].predictedVelAndDensity.x, pcisphData[gid].predictedVelAndDensity.y, pcisphData[gid].predictedVelAndDensity.z);
                float penetration = fmaxf(0.0f, h - sdf);
                float3 f = params.u_BoundaryParams.x * penetration * worldNormal
                         - params.u_BoundaryParams.y * dot(velRel, worldNormal) * worldNormal;
                fBoundary += f * blend;
            }
        }
    }

    float warmupTime = params.u_BoundaryParams.z;
    float age = particles[myParticleIdx].velAndMaxLife.w - particles[myParticleIdx].posAndLife.w;
    float warmup = (warmupTime > 0.0f) ? clamp(age / warmupTime, 0.0f, 1.0f) : 1.0f;

    float3 a_pressure = fPressure * warmup + fBoundary / densityI;
    float accelMag = length(a_pressure);
    if (accelMag > 500.0f) a_pressure *= 500.0f / accelMag;

    pcisphData[gid].predictedVelAndDensity.x += a_pressure.x * params.u_DeltaTime;
    pcisphData[gid].predictedVelAndDensity.y += a_pressure.y * params.u_DeltaTime;
    pcisphData[gid].predictedVelAndDensity.z += a_pressure.z * params.u_DeltaTime;
}

// 7. PCISPH Apply Kernel
__global__ void sph_pcisph_apply_kernel(
    GPUParticle* __restrict__ particles,
    const PCISPHData* __restrict__ pcisphData,
    const unsigned int* __restrict__ aliveIndices,
    SPHParams params)
{
    unsigned int gid = blockIdx.x * blockDim.x + threadIdx.x;
    if (gid >= params.u_AliveCount) return;

    unsigned int myParticleIdx = aliveIndices[gid];

    particles[myParticleIdx].velAndMaxLife.x = pcisphData[gid].predictedVelAndDensity.x;
    particles[myParticleIdx].velAndMaxLife.y = pcisphData[gid].predictedVelAndDensity.y;
    particles[myParticleIdx].velAndMaxLife.z = pcisphData[gid].predictedVelAndDensity.z;

    particles[myParticleIdx].posAndLife.x = pcisphData[gid].predictedPosAndPressure.x;
    particles[myParticleIdx].posAndLife.y = pcisphData[gid].predictedPosAndPressure.y;
    particles[myParticleIdx].posAndLife.z = pcisphData[gid].predictedPosAndPressure.z;
}
