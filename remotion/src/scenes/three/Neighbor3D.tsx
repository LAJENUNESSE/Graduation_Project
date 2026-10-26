import { useCurrentFrame, interpolate } from "remotion";
import { useMemo } from "react";
import * as THREE from "three";
import { COLOR, FONT } from "../../theme";
import { seeded } from "../../components/Common";
import type { Caption } from "../../components/Scene";
import { CenterParticle, KernelSupportSphere, Scene3D, Scene3DShot } from "./Scene3D";

/**
 * 3D 支撑球域：半径 h 的球内粒子才参与加权求和。
 *
 * 平面图只能画个圆，而 SPH 的支撑域是球——只有三维里才能看出
 * 「球外粒子权重恒为零」这件事。
 */
export const KernelSphere3D: React.FC<{
  index?: string;
  title?: string;
  anchor?: string;
  captions?: Caption[];
  withChrome?: boolean;
}> = ({ index, title, anchor, captions, withChrome = false }) => {
  const frame = useCurrentFrame();

  // 相机缓慢环绕，立体感主要来自视差
  const angle = interpolate(frame, [0, 360], [0.5, 2.1], {
    extrapolateRight: "clamp",
  });
  const radius = 6.2;

  const particles = useMemo(() => {
    const rand = seeded(31415);
    const list: { pos: [number, number, number]; inside: boolean; w: number }[] = [];
    for (let i = 0; i < 130; i++) {
      const p: [number, number, number] = [
        (rand() * 2 - 1) * 3.1,
        (rand() * 2 - 1) * 3.1,
        (rand() * 2 - 1) * 3.1,
      ];
      const r = Math.hypot(p[0], p[1], p[2]) / 2.2;
      const inside = r < 1;
      // Poly6：(1 - (r/h)^2)^3
      const w = inside ? Math.pow(1 - r * r, 3) : 0;
      list.push({ pos: p, inside, w });
    }
    return list;
  }, []);

  const scene = (
    <Scene3D
      camera={{
        position: [
          Math.sin(angle) * radius,
          3.4,
          Math.cos(angle) * radius,
        ],
      }}
    >
      <KernelSupportSphere frame={frame} radius={2.2} color={COLOR.accent} />
      <CenterParticle position={[0, 0, 0]} />

      {/* 球内粒子按权重着色，球外粒子灰暗不参与计算 */}
      {particles.map((p, i) => (
        <mesh key={i} position={p.pos}>
          <sphereGeometry args={[0.075, 12, 12]} />
          <meshStandardMaterial
            color={p.inside ? COLOR.orange : "#B4B0A6"}
            transparent
            opacity={p.inside ? 0.35 + p.w * 0.65 : 0.3}
            emissive={p.inside ? COLOR.orange : "#000000"}
            emissiveIntensity={p.inside ? p.w * 0.6 : 0}
          />
        </mesh>
      ))}
    </Scene3D>
  );

  if (!withChrome) return scene;

  return (
    <Scene3DShot
      index={index}
      title={title}
      anchor={anchor}
      captions={captions}
      hud={<SphereHud frame={frame} />}
    >
      {scene}
    </Scene3DShot>
  );
};

/** 右下角图例：区分球内 / 球外粒子 */
const SphereHud: React.FC<{ frame: number }> = ({ frame }) => {
  const t = interpolate(frame, [40, 70], [0, 1], { extrapolateRight: "clamp" });
  return (
    <div
      style={{
        position: "absolute",
        right: 56,
        top: 130,
        opacity: t,
        backgroundColor: "rgba(255,255,255,0.92)",
        borderRadius: 10,
        padding: "18px 22px",
        fontFamily: FONT.sans,
        fontSize: 19,
        lineHeight: 1.9,
        boxShadow: "0 6px 24px rgba(0,0,0,0.07)",
      }}
    >
      <div style={{ fontWeight: 700, marginBottom: 8, color: COLOR.ink }}>
        支撑域内外的区别
      </div>
      <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
        <span
          style={{
            width: 14,
            height: 14,
            borderRadius: "50%",
            backgroundColor: COLOR.orange,
            display: "inline-block",
          }}
        />
        球内：权重 W &gt; 0，参与累加
      </div>
      <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
        <span
          style={{
            width: 14,
            height: 14,
            borderRadius: "50%",
            backgroundColor: "#B4B0A6",
            display: "inline-block",
          }}
        />
        球外：W = 0，完全跳过
      </div>
      <div style={{ fontSize: 17, color: COLOR.inkMuted, marginTop: 10, maxWidth: 260 }}>
        半径 h 之外权重恒为零，是 SPH 能只查局部邻域的前提
      </div>
    </div>
  );
};

/**
 * 3D 空间哈希：3×3×3 共 27 个邻域格子。
 *
 * 必须用三维表达——平面只能画 3×3 九个格子，而真实算法是 27 个，
 * 少一维会让人严重低估邻居搜索的实际工作量。
 */
export const HashGrid3D: React.FC<{
  index?: string;
  title?: string;
  anchor?: string;
  captions?: Caption[];
  withChrome?: boolean;
}> = ({ index, title, anchor, captions, withChrome = false }) => {
  const frame = useCurrentFrame();
  const cellSize = 1.15;

  const cameraAngle = interpolate(frame, [0, 420], [0.62, 1.85], {
    extrapolateRight: "clamp",
  });

  const particles = useMemo(() => {
    const rand = seeded(90210);
    const list: { pos: [number, number, number] }[] = [];
    for (let i = 0; i < 160; i++) {
      const cx = Math.floor(rand() * 3);
      const cy = Math.floor(rand() * 3);
      const cz = Math.floor(rand() * 3);
      list.push({
        pos: [
          (cx - 1) * cellSize + (rand() - 0.5) * cellSize * 0.72,
          (cy - 1) * cellSize + (rand() - 0.5) * cellSize * 0.72,
          (cz - 1) * cellSize + (rand() - 0.5) * cellSize * 0.72,
        ],
      });
    }
    return list;
  }, []);

  const cells: { key: string; pos: [number, number, number]; order: number }[] = [];
  let order = 0;
  for (let z = 0; z < 3; z++) {
    for (let y = 0; y < 3; y++) {
      for (let x = 0; x < 3; x++) {
        cells.push({
          key: `${x},${y},${z}`,
          pos: [(x - 1) * cellSize, (y - 1) * cellSize, (z - 1) * cellSize],
          order: order++,
        });
      }
    }
  }

  const scene = (
    <Scene3D
      camera={{
        position: [
          Math.sin(cameraAngle) * 7.4,
          4.3,
          Math.cos(cameraAngle) * 7.4,
        ],
      }}
    >
      {cells.map((c) => {
        const lit = interpolate(
          frame,
          [30 + c.order * 5, 52 + c.order * 5],
          [0.16, 0.85],
          { extrapolateLeft: "clamp", extrapolateRight: "clamp" },
        );
        const isCenter =
          c.pos[0] === 0 && c.pos[1] === 0 && c.pos[2] === 0;
        return (
          <group key={c.key} position={c.pos}>
            {/* 半透明实体面：让格子的体积感立起来 */}
            <mesh>
              <boxGeometry args={[cellSize * 0.97, cellSize * 0.97, cellSize * 0.97]} />
              <meshStandardMaterial
                color={isCenter ? COLOR.orange : COLOR.accent}
                transparent
                opacity={isCenter ? 0.22 : lit * 0.13}
                depthWrite={false}
                roughness={0.85}
              />
            </mesh>
            {/* 加粗棱线：远距离下仍能看清 27 个格子的结构 */}
            <lineSegments>
              <edgesGeometry args={[new THREE.BoxGeometry(cellSize, cellSize, cellSize)]} />
              <lineBasicMaterial
                color={isCenter ? COLOR.orange : COLOR.accent}
                transparent
                opacity={isCenter ? 0.95 : lit}
              />
            </lineSegments>
          </group>
        );
      })}

      {particles.map((p, i) => (
        <mesh key={i} position={p.pos}>
          <sphereGeometry args={[0.085, 14, 14]} />
          <meshStandardMaterial
            color={COLOR.inkSoft}
            roughness={0.35}
            metalness={0.15}
          />
        </mesh>
      ))}

      <CenterParticle position={[0, 0, 0]} radius={0.13} />
    </Scene3D>
  );

  if (!withChrome) return scene;

  return (
    <Scene3DShot
      index={index}
      title={title}
      anchor={anchor}
      captions={captions}
      hud={<HashHud frame={frame} />}
    >
      {scene}
    </Scene3DShot>
  );
};

const HashHud: React.FC<{ frame: number }> = ({ frame }) => {
  const count = 27;
  const revealed = Math.floor(
    interpolate(frame, [30, 190], [0, count], {
      extrapolateLeft: "clamp",
      extrapolateRight: "clamp",
    }),
  );
  return (
    <div
      style={{
        position: "absolute",
        right: 56,
        top: 130,
        backgroundColor: "rgba(255,255,255,0.92)",
        borderRadius: 10,
        padding: "18px 24px",
        fontFamily: FONT.sans,
        boxShadow: "0 6px 24px rgba(0,0,0,0.07)",
      }}
    >
      <div style={{ fontSize: 19, color: COLOR.inkMuted }}>已点亮格子</div>
      <div
        style={{
          fontFamily: FONT.serif,
          fontSize: 42,
          fontWeight: 700,
          color: COLOR.accent,
          lineHeight: 1.2,
        }}
      >
        {revealed}
        <span style={{ fontSize: 24, color: COLOR.inkMuted }}> / 27</span>
      </div>
      <div style={{ fontSize: 17, color: COLOR.inkMuted, maxWidth: 240, marginTop: 6 }}>
        平面示意图只画 9 格，三维实际是 27 格——邻居遍历量是 3 倍
      </div>
    </div>
  );
};
