/**
 * 供三维液面渲染使用的粒子数据。
 *
 * 物理参数与仓库实现对齐：
 *   - 核函数系数取自 Engine/src/Renderer/SPHKernelMath.h
 *   - 默认 h / m / ρ₀ 取自 Engine/src/Scene/Components.h 的流体发射器
 *
 * 数据本身确定性预生成（不用 Math.random），因为 Remotion 逐帧独立渲染，
 * 任何随机都会在成片里表现为闪烁。
 */
import { seeded } from "../../components/Common";

/** 与 SPHKernelMath::Compute 一致的核系数 */
export const KERNEL = {
  h: 0.1,
  poly6Coeff: 315 / (64 * Math.PI * Math.pow(0.1, 9)),
  spikyCoeff: -45 / (Math.PI * Math.pow(0.1, 6)),
};

/** 流体发射器默认参数（Components.h） */
export const FLUID = {
  restDensity: 1000,
  gasConstant: 50,
  viscosity: 3.5,
  smoothingRadius: 0.1,
  particleMass: 0.02,
  gravity: -9.81,
};

export type Particle = {
  x: number;
  y: number;
  z: number;
};

/**
 * 生成一柱会崩塌的水体（dam break）。
 *
 * 用简化的 SPH 力模型积分：重力 + 近邻排斥 + 粘性 + 壁面碰撞。
 * 粒子数刻意控制在约 600，因为片元着色器每个像素都要遍历全部粒子，
 * 数量直接决定渲染成本。
 */
export const simulateDamBreak = ({
  frames = 360,
  seed = 13579,
}: { frames?: number; seed?: number } = {}): Particle[][] => {
  const rand = seeded(seed);
  // 粒子数直接决定着色器成本（每个像素都要遍历全部粒子）。
  // 控制在约 300 个：液面看起来依然连续，单帧耗时却大幅下降。
  const NX = 6;
  const NZ = 7;
  const NY = 8;
  const spacing = 0.135;
  const groundY = -0.75;

  type P = {
    x: number;
    y: number;
    z: number;
    vx: number;
    vy: number;
    vz: number;
  };

  const ps: P[] = [];
  // 初始：靠左的水柱，向右崩塌
  for (let y = 0; y < NY; y++) {
    for (let z = 0; z < NZ; z++) {
      for (let x = 0; x < NX; x++) {
        ps.push({
          x: (x - NX / 2) * spacing - 0.42,
          y: groundY + 0.06 + y * spacing,
          z: (z - NZ / 2) * spacing,
          vx: 0,
          vy: 0,
          vz: 0,
        });
      }
    }
  }

  const dt = 1 / 120;
  const h = spacing * 2.1;
  const restDist = spacing * 1.0;
  const limit = 0.8;
  const out: Particle[][] = [];

  for (let f = 0; f < frames; f++) {
    const ax = new Float64Array(ps.length);
    const ay = new Float64Array(ps.length);
    const az = new Float64Array(ps.length);

    for (let i = 0; i < ps.length; i++) {
      const p = ps[i];
      for (let j = i + 1; j < ps.length; j++) {
        const q = ps[j];
        const dx = p.x - q.x;
        const dy = p.y - q.y;
        const dz = p.z - q.z;
        const d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > h * h || d2 < 1e-12) continue;
        const d = Math.sqrt(d2);

        // 压力型排斥：仅当距离小于静止间距
        const overlap = restDist - d;
        if (overlap > 0) {
          const force = overlap * 640;
          const nx = dx / d;
          const ny = dy / d;
          const nz = dz / d;
          ax[i] += nx * force;
          ay[i] += ny * force;
          az[i] += nz * force;
          ax[j] -= nx * force;
          ay[j] -= ny * force;
          az[j] -= nz * force;
        }

        // 粘性
        const rvx = q.vx - p.vx;
        const rvy = q.vy - p.vy;
        const rvz = q.vz - p.vz;
        const visc = 30;
        ax[i] += rvx * visc;
        ay[i] += rvy * visc;
        az[i] += rvz * visc;
        ax[j] -= rvx * visc;
        ay[j] -= rvy * visc;
        az[j] -= rvz * visc;
      }
    }

    for (let i = 0; i < ps.length; i++) {
      const p = ps[i];
      p.vy += FLUID.gravity * dt;
      p.vx += ax[i] * dt;
      p.vy += ay[i] * dt;
      p.vz += az[i] * dt;

      const damp = 0.986;
      p.vx *= damp;
      p.vy *= damp;
      p.vz *= damp;

      p.x += p.vx * dt;
      p.y += p.vy * dt;
      p.z += p.vz * dt;

      // 容器碰撞
      if (p.y < groundY) {
        p.y = groundY;
        if (p.vy < 0) p.vy *= -0.25;
        p.vx *= 0.95;
        p.vz *= 0.95;
      }
      if (p.x < -limit) {
        p.x = -limit;
        p.vx *= -0.3;
      }
      if (p.x > limit) {
        p.x = limit;
        p.vx *= -0.3;
      }
      if (p.z < -limit) {
        p.z = -limit;
        p.vz *= -0.3;
      }
      if (p.z > limit) {
        p.z = limit;
        p.vz *= -0.3;
      }

      // 极小的确定性扰动，模拟数值噪声
      p.x += (rand() - 0.5) * 0.0005;
      p.z += (rand() - 0.5) * 0.0005;
    }

    out.push(ps.map((p) => ({ x: p.x, y: p.y, z: p.z })));
  }

  return out;
};

/**
 * 把粒子位置平铺进纹理，供着色器采样。
 *
 * 纹理宽度固定 512：粒子数上限也按此设定，
 * 采样时用 1D 坐标直接定位，避免循环里做除法。
 */
export const TEX_WIDTH = 512;

export const particlesToTexture = (particles: Particle[]): Float32Array => {
  const data = new Float32Array(TEX_WIDTH * 4);
  for (let i = 0; i < Math.min(particles.length, TEX_WIDTH); i++) {
    data[i * 4 + 0] = particles[i].x;
    data[i * 4 + 1] = particles[i].y;
    data[i * 4 + 2] = particles[i].z;
    data[i * 4 + 3] = 1;
  }
  return data;
};
