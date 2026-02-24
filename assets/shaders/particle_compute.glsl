#type compute
#version 430 core

layout(local_size_x = 256) in;

struct Particle
{
    vec4 position;     // xyz = position, w = size
    vec4 velocity;     // xyz = velocity, w = remaining life
    vec4 color;        // current RGBA
    vec4 colorStart;   // start RGBA
    vec4 colorEnd;     // end RGBA
    vec4 params;       // x = maxLife, y = alive(1/0), z = rotation, w = angular velocity
};

layout(std430, binding = 0) buffer ParticleBuffer
{
    Particle particles[];
};

layout(std430, binding = 1) buffer IndirectBuffer
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint baseInstance;
    uint emittedThisFrame;
};

layout(std430, binding = 2) buffer AliveBuffer
{
    uint aliveIndices[];
};

uniform float u_DeltaTime;
uniform vec3  u_EmitterPosition;
uniform vec3  u_EmitterDirection;
uniform float u_SpreadAngle;
uniform float u_SpeedMin;
uniform float u_SpeedMax;
uniform float u_LifeMin;
uniform float u_LifeMax;
uniform float u_SizeStart;
uniform float u_SizeEnd;
uniform vec4  u_ColorStart;
uniform vec4  u_ColorEnd;
uniform vec3  u_Gravity;
uniform int   u_EmitCount;
uniform int   u_MaxParticles;
uniform int   u_RandomSeed;

// PCG hash for random number generation
uint pcgHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float randomFloat(inout uint seed)
{
    seed = pcgHash(seed);
    return float(seed) / 4294967295.0;
}

vec3 randomDirection(inout uint seed, vec3 baseDir, float spreadAngle)
{
    float cosAngle = cos(spreadAngle);
    float z = mix(cosAngle, 1.0, randomFloat(seed));
    float phi = 2.0 * 3.14159265 * randomFloat(seed);
    float sinTheta = sqrt(1.0 - z * z);

    vec3 localDir = vec3(sinTheta * cos(phi), sinTheta * sin(phi), z);

    // Build TBN from baseDir
    vec3 up = abs(baseDir.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, baseDir));
    vec3 bitangent = cross(baseDir, tangent);

    return normalize(tangent * localDir.x + bitangent * localDir.y + baseDir * localDir.z);
}

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= uint(u_MaxParticles)) return;

    Particle p = particles[idx];

    // --- Emit: try to respawn dead particles ---
    if (p.params.y < 0.5)
    {
        uint claimed = atomicAdd(emittedThisFrame, 1);
        if (claimed < uint(u_EmitCount))
        {
            uint seed = uint(u_RandomSeed) + idx * 1799u + claimed * 3571u;

            vec3 dir = randomDirection(seed, normalize(u_EmitterDirection), radians(u_SpreadAngle));
            float speed = mix(u_SpeedMin, u_SpeedMax, randomFloat(seed));
            float life = mix(u_LifeMin, u_LifeMax, randomFloat(seed));

            p.position = vec4(u_EmitterPosition, u_SizeStart);
            p.velocity = vec4(dir * speed, life);
            p.color = u_ColorStart;
            p.colorStart = u_ColorStart;
            p.colorEnd = u_ColorEnd;
            p.params = vec4(life, 1.0, 0.0, 0.0);
        }
        // else: stays dead, claimed is "wasted" but harmless
    }

    // --- Simulate: update alive particles ---
    if (p.params.y > 0.5)
    {
        // Apply gravity
        p.velocity.xyz += u_Gravity * u_DeltaTime;

        // Integrate position
        p.position.xyz += p.velocity.xyz * u_DeltaTime;

        // Decrement life
        p.velocity.w -= u_DeltaTime;

        if (p.velocity.w <= 0.0)
        {
            // Kill particle
            p.params.y = 0.0;
        }
        else
        {
            // Interpolate color and size
            float t = 1.0 - p.velocity.w / p.params.x;
            t = clamp(t, 0.0, 1.0);
            p.color = mix(p.colorStart, p.colorEnd, t);
            p.position.w = mix(u_SizeStart, u_SizeEnd, t);

            // Add to alive list for rendering
            uint aliveIdx = atomicAdd(instanceCount, 1);
            aliveIndices[aliveIdx] = idx;
        }
    }

    particles[idx] = p;
}
