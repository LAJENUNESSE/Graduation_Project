import { useCurrentFrame, interpolate } from "remotion";
import * as THREE from "three";
import { useMemo } from "react";
import { COLOR, FONT } from "../../theme";
import { seeded } from "../../components/Common";
import type { Caption } from "../../components/Scene";
import { Ground, Scene3D, Scene3DShot } from "./Scene3D";

/** 地面高度，提为模块常量以便 useMemo 依赖稳定 */
const PCISPH_GROUND_Y = -1.9;

/**
 * PCISPH 的三维可视化：一柱粒子在重力下压向地面，密度超调后
 * 压力把它们推回去，逐轮迭代后体积回到接近 ρ₀。
 *
 * 这件事必须在三维里看：穿透、堆积、侧向挤出都是空间行为，
 * 二维剖面会把「体积守恒」这件事完全抹掉。
 */
export const PCISPH3D: React.FC<{
  index?: string;
  title?: string;
  anchor?: string;
  captions?: Caption[];
  withChrome?: boolean;
}> = ({ index, title, anchor, captions, withChrome = false }) => {
  const frame = useCurrentFrame();

  const baseCount = 7;
  const spacing = 0.42;

  const lattice = useMemo(() => {
    const pts: [number, number, number][] = [];
    for (let y = 0; y < 5; y++) {
      for (let z = 0; z < baseCount; z++) {
        for (let x = 0; x < baseCount; x++) {
          pts.push([
            (x - (baseCount - 1) / 2) * spacing,
            PCISPH_GROUND_Y + 0.22 + y * spacing,
            (z - (baseCount - 1) / 2) * spacing,
          ]);
        }
      }
    }
    return pts;
  }, []);

  const rand = useMemo(() => seeded(777), []);

  /**
   * 节奏：
   *   20-120   自由下压，密度开始超调
   *   120-140  过压峰值
   *   140-330  逐轮迭代校正，体积恢复
   */
  const compression = interpolate(frame, [20, 120], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
    easing: (t) => t * t,
  });
  const correction = interpolate(frame, [140, 330], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
    easing: (t) => 1 - Math.pow(1 - t, 3),
  });

  // 压缩率：先压扁，再逐步恢复
  const squash = 0.42 * compression * (1 - 0.88 * correction);
  // 侧向膨胀：体积守恒的直观体现
  const bulge = 1 + 0.5 * compression * (1 - 0.75 * correction);

  const positions = lattice.map(([x, y, z]) => {
    const jitter = rand();
    const h = y - PCISPH_GROUND_Y;
    // 越靠下压缩越明显（上方粒子还在往下压）
    const depthFactor = 1 - Math.min(1, h / (5 * spacing));
    const compress = 1 - squash * (0.35 + depthFactor * 0.65);
    const newY = PCISPH_GROUND_Y + h * compress;
    const spread = 1 + (bulge - 1) * (0.4 + depthFactor * 0.6);
    const wob =
      Math.sin(frame * 0.18 + jitter * 6.28) *
      0.03 *
      compression *
      (1 - correction);
    return [x * spread + wob, newY, z * spread + wob] as [number, number, number];
  });

  const error = 240 * compression * (1 - correction) * (1 - correction) + 6;

  const cameraAngle = interpolate(frame, [0, 380], [0.75, 1.25], {
    extrapolateRight: "clamp",
  });

  const scene = (
    <Scene3D
      camera={{
        position: [
          Math.sin(cameraAngle) * 7.2,
          2.6,
          Math.cos(cameraAngle) * 7.2,
        ],
        fov: 40,
      }}
    >
      <Ground y={PCISPH_GROUND_Y - 0.06} />

      {positions.map((p, i) => {
        // 局部密度越高越偏红：被压得越狠，颜色越警示
        const localDensity = 1 - Math.min(1, (p[1] - PCISPH_GROUND_Y) / (5 * spacing));
        const heat = compression * (1 - correction) * localDensity;
        const c = new THREE.Color(COLOR.accent).lerp(
          new THREE.Color(COLOR.red),
          Math.min(0.85, heat * 1.3),
        );
        return (
          <mesh key={i} position={p}>
            <sphereGeometry args={[0.19, 20, 20]} />
            <meshStandardMaterial color={c} roughness={0.32} metalness={0.12} />
          </mesh>
        );
      })}
    </Scene3D>
  );

  if (!withChrome) return scene;

  return (
    <Scene3DShot
      index={index}
      title={title}
      anchor={anchor}
      captions={captions}
      hud={<PCISPHHud frame={frame} error={error} />}
    >
      {scene}
    </Scene3DShot>
  );
};

const PCISPHHud: React.FC<{ frame: number; error: number }> = ({
  frame,
  error,
}) => {
  const iterTotal = 6;
  const iterNow = Math.min(
    iterTotal,
    Math.max(
      0,
      Math.floor(
        interpolate(frame, [140, 330], [0, iterTotal], {
          extrapolateLeft: "clamp",
          extrapolateRight: "clamp",
        }),
      ),
    ),
  );

  const phase =
    frame < 90
      ? "下压：密度开始超调"
      : frame < 140
        ? "过压：误差达到峰值"
        : "迭代校正：把 ρ* 压回 ρ₀";

  return (
    <div
      style={{
        position: "absolute",
        right: 56,
        top: 130,
        width: 400,
        backgroundColor: "rgba(255,255,255,0.93)",
        borderRadius: 10,
        padding: "20px 24px",
        fontFamily: FONT.sans,
        boxShadow: "0 6px 24px rgba(0,0,0,0.08)",
      }}
    >
      <div
        style={{
          fontSize: 21,
          fontWeight: 700,
          color: COLOR.ink,
          marginBottom: 4,
        }}
      >
        {phase}
      </div>

      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          alignItems: "baseline",
          fontSize: 19,
          margin: "14px 0 6px",
        }}
      >
        <span style={{ color: COLOR.inkMuted }}>预测密度 ρ* 误差</span>
        <span
          style={{
            fontFamily: FONT.serif,
            fontSize: 26,
            fontWeight: 700,
            color: error > 60 ? COLOR.red : COLOR.green,
          }}
        >
          {error.toFixed(0)}
        </span>
      </div>

      <div
        style={{
          height: 12,
          backgroundColor: COLOR.bgAlt,
          borderRadius: 3,
          overflow: "hidden",
        }}
      >
        <div
          style={{
            width: `${Math.min(100, (error / 260) * 100)}%`,
            height: "100%",
            backgroundColor: error > 60 ? COLOR.red : COLOR.green,
            borderRadius: 3,
          }}
        />
      </div>

      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          fontSize: 19,
          marginTop: 16,
        }}
      >
        <span style={{ color: COLOR.inkMuted }}>PCISPH 迭代</span>
        <span style={{ fontFamily: FONT.mono, fontSize: 22, color: COLOR.ink }}>
          {iterNow} / {iterTotal}
        </span>
      </div>

      <div
        style={{
          fontSize: 17,
          color: COLOR.inkMuted,
          marginTop: 14,
          lineHeight: 1.6,
        }}
      >
        粒子压得越扁、颜色越红，代表局部密度越高于 ρ₀；迭代把体积推回去，颜色随之恢复。
      </div>
    </div>
  );
};

/**
 * 三维流体柱崩塌（dam break 简化版）。
 *
 * 用 SPH 的力模型思想近似积分：重力 + 近邻排斥 + 粘性阻尼 + 壁面碰撞。
 * 不是完整的 PCISPH 求解，但运动学关系是对的——目的是让观众看到
 * 「压力让粒子互相排斥、粘性让速度趋同」在三维里长什么样。
 */
export const FluidColumn3D: React.FC<{
  index?: string;
  title?: string;
  anchor?: string;
  captions?: Caption[];
  withChrome?: boolean;
}> = ({ index, title, anchor, captions, withChrome = false }) => {
  const frame = useCurrentFrame();
  const N = 6;
  const spacing = 0.3;

  /**
   * 预积分整段模拟。
   *
   * 必须一次性算完而不是逐帧增量：Remotion 会并发、乱序渲染各帧，
   * 任何依赖「上一帧状态」的写法在并发下都会错乱。
   */
  const frames = useMemo(() => {
    const rand = seeded(13579);
    type P = {
      x: number;
      y: number;
      z: number;
      vx: number;
      vy: number;
      vz: number;
    };
    const particles: P[] = [];
    // 初始：一个偏高的水柱，靠左放置以便向右崩塌
    for (let y = 0; y < 8; y++) {
      for (let z = 0; z < N; z++) {
        for (let x = 0; x < N; x++) {
          particles.push({
            x: (x - (N - 1) / 2) * spacing - 1.1,
            y: PCISPH_GROUND_Y + 0.2 + y * spacing,
            z: (z - (N - 1) / 2) * spacing,
            vx: 0,
            vy: 0,
            vz: 0,
          });
        }
      }
    }

    const totalFrames = 380;
    const snapshots: { x: number; y: number; z: number }[][] = [];
    const dt = 1 / 60;
    const h = spacing * 1.9;
    const restDist = spacing;

    for (let f = 0; f < totalFrames; f++) {
      const ax = new Float64Array(particles.length);
      const ay = new Float64Array(particles.length);
      const az = new Float64Array(particles.length);

      for (let i = 0; i < particles.length; i++) {
        const p = particles[i];
        for (let j = i + 1; j < particles.length; j++) {
          const q = particles[j];
          const dx = p.x - q.x;
          const dy = p.y - q.y;
          const dz = p.z - q.z;
          const d2 = dx * dx + dy * dy + dz * dz;
          if (d2 > h * h || d2 < 1e-9) continue;
          const d = Math.sqrt(d2);

          // 压力型排斥：距离小于静止间距时强烈互推
          const overlap = restDist - d;
          if (overlap > 0) {
            const force = overlap * 180;
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

          // 粘性：把邻近粒子速度往彼此拉
          const rvx = q.vx - p.vx;
          const rvy = q.vy - p.vy;
          const rvz = q.vz - p.vz;
          const visc = 26;
          ax[i] += rvx * visc;
          ay[i] += rvy * visc;
          az[i] += rvz * visc;
          ax[j] -= rvx * visc;
          ay[j] -= rvy * visc;
          az[j] -= rvz * visc;
        }
      }

      for (let i = 0; i < particles.length; i++) {
        const p = particles[i];
        p.vy += -9.81 * dt;
        p.vx += ax[i] * dt;
        p.vy += ay[i] * dt;
        p.vz += az[i] * dt;
        const damp = 0.982;
        p.vx *= damp;
        p.vy *= damp;
        p.vz *= damp;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.z += p.vz * dt;

        // 地面与四壁碰撞
        if (p.y < PCISPH_GROUND_Y) {
          p.y = PCISPH_GROUND_Y;
          if (p.vy < 0) p.vy *= -0.22;
        }
        const limit = 3.1;
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

        // 极少量确定性抖动，模拟数值噪声
        p.x += (rand() - 0.5) * 0.0016;
        p.z += (rand() - 0.5) * 0.0016;
      }

      snapshots.push(particles.map((p) => ({ x: p.x, y: p.y, z: p.z })));
    }
    return snapshots;
  }, []);

  const idx = Math.min(
    frames.length - 1,
    Math.max(0, Math.floor((frame / 380) * frames.length)),
  );
  const current = frames[idx];

  const angle = interpolate(frame, [0, 380], [0.55, 1.35], {
    extrapolateRight: "clamp",
  });
  const height = interpolate(frame, [0, 380], [2.2, 3.2], {
    extrapolateRight: "clamp",
  });

  const lowColor = useMemo(() => new THREE.Color("#1E5A8A"), []);
  const highColor = useMemo(() => new THREE.Color("#6FB3D9"), []);

  const scene = (
    <Scene3D
      camera={{
        position: [Math.sin(angle) * 8.4, height, Math.cos(angle) * 8.4],
        fov: 40,
      }}
    >
      <Ground y={PCISPH_GROUND_Y - 0.04} />

      {current?.map((p, i) => {
        const t = Math.min(1, Math.max(0, (p.y - PCISPH_GROUND_Y) / 2.6));
        const c = lowColor.clone().lerp(highColor, t);
        return (
          <mesh key={i} position={[p.x, p.y, p.z]}>
            <sphereGeometry args={[0.15, 16, 16]} />
            <meshStandardMaterial color={c} roughness={0.22} metalness={0.25} />
          </mesh>
        );
      })}
    </Scene3D>
  );

  if (!withChrome) return scene;

  return (
    <Scene3DShot index={index} title={title} anchor={anchor} captions={captions}>
      {scene}
    </Scene3DShot>
  );
};
