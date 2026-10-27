import { ThreeCanvas } from "@remotion/three";
import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { COLOR, FONT, VIDEO } from "../../theme";
import { useMemo } from "react";
import * as THREE from "three";
import { seeded } from "../../components/Common";
import type { Caption } from "../../components/Scene";

/**
 * 3D 镜头的基础画布：统一相机、光照、背景。
 *
 * SPH 的支撑球域、27 格邻域、粒子堆积都是三维现象，
 * 用平面示意图会丢掉「球」「27 个格子」这类关键结构。
 */
export const Scene3D: React.FC<{
  camera?: { position: [number, number, number]; fov?: number };
  background?: string;
  children: React.ReactNode;
  /** 右上角 HUD，用于放置实时读数面板 */
  hud?: React.ReactNode;
}> = ({
  camera = { position: [4.2, 3.0, 5.4], fov: 42 },
  background = "#F4F3EE",
  children,
  hud,
}) => {
  return (
    <AbsoluteFill style={{ backgroundColor: background }}>
      <ThreeCanvas
        width={VIDEO.width}
        height={VIDEO.height}
        camera={{ position: camera.position, fov: camera.fov ?? 42 }}
        style={{ backgroundColor: background }}
        gl={{ antialias: true }}
      >
        <color attach="background" args={[background]} />
        <ambientLight intensity={0.85} />
        <directionalLight position={[6, 9, 5]} intensity={2.0} />
        <directionalLight position={[-5, 3, -4]} intensity={0.7} color="#BBD6EE" />
        {children}
      </ThreeCanvas>
      {hud}
    </AbsoluteFill>
  );
};

/**
 * 3D 镜头的叠加层：只负责标题、HUD 与字幕，不含画布。
 *
 * 需要自己控制 ThreeCanvas（自定义相机、着色器材质）的镜头用它，
 * 其余镜头直接用下面的 Scene3DShot 组合即可。
 */
export const ShotChrome: React.FC<{
  index?: string;
  title?: string;
  anchor?: string;
  captions?: Caption[];
  hud?: React.ReactNode;
  children: React.ReactNode;
}> = ({ index, title, anchor, captions = [], hud, children }) => {
  const frame = useCurrentFrame();
  const fadeIn = interpolate(frame, [0, 14], [0, 1], { extrapolateRight: "clamp" });

  return (
    <AbsoluteFill style={{ fontFamily: FONT.sans, color: COLOR.ink }}>
      {children}
      <TitleBar index={index} title={title} anchor={anchor} fadeIn={fadeIn} />
      {hud}
      <ShotCaptions captions={captions} />
    </AbsoluteFill>
  );
};

const TitleBar: React.FC<{
  index?: string;
  title?: string;
  anchor?: string;
  fadeIn: number;
}> = ({ index, title, anchor, fadeIn }) => (
  <div
    style={{
      position: "absolute",
      top: 0,
      left: 0,
      right: 0,
      padding: "26px 96px 22px",
      display: "flex",
      alignItems: "baseline",
      gap: 20,
      background:
        "linear-gradient(to bottom, rgba(255,255,255,0.95), rgba(255,255,255,0))",
      opacity: fadeIn,
    }}
  >
    {index ? (
      <span
        style={{
          fontFamily: FONT.serif,
          fontSize: 40,
          fontWeight: 700,
          color: COLOR.accent,
          letterSpacing: 2,
        }}
      >
        {index}
      </span>
    ) : null}
    {title ? (
      <span
        style={{
          fontSize: 40,
          fontWeight: 600,
          letterSpacing: 1,
          textShadow: "0 2px 12px rgba(255,255,255,0.9)",
        }}
      >
        {title}
      </span>
    ) : null}
    <span style={{ flex: 1 }} />
    {anchor ? (
      <span
        style={{
          fontFamily: FONT.mono,
          fontSize: 18,
          color: COLOR.inkMuted,
        }}
      >
        {anchor}
      </span>
    ) : null}
  </div>
);

/**
 * 全幅 3D 镜头版式：3D 铺满 1920×1080，标题与字幕作为叠加层浮在上面。
 *
 * 与 2D 的 Scene 保持同一套视觉语言（同样的标题位置、同样的字幕条），
 * 这样 2D 与 3D 镜头交替出现时不会显得像两支不同的片子。
 */
export const Scene3DShot: React.FC<{
  index?: string;
  title?: string;
  anchor?: string;
  captions?: Caption[];
  hud?: React.ReactNode;
  children: React.ReactNode;
}> = ({ index, title, anchor, captions = [], hud, children }) => {
  const frame = useCurrentFrame();
  const fadeIn = interpolate(frame, [0, 14], [0, 1], { extrapolateRight: "clamp" });

  return (
    <AbsoluteFill style={{ fontFamily: FONT.sans, color: COLOR.ink }}>
      {children}

      {/* 顶部标题条：半透明白底，保证压在 3D 画面上依然可读 */}
      <div
        style={{
          position: "absolute",
          top: 0,
          left: 0,
          right: 0,
          padding: "26px 96px 22px",
          display: "flex",
          alignItems: "baseline",
          gap: 20,
          background:
            "linear-gradient(to bottom, rgba(255,255,255,0.95), rgba(255,255,255,0))",
          opacity: fadeIn,
        }}
      >
        {index ? (
          <span
            style={{
              fontFamily: FONT.serif,
              fontSize: 40,
              fontWeight: 700,
              color: COLOR.accent,
              letterSpacing: 2,
            }}
          >
            {index}
          </span>
        ) : null}
        {title ? (
          <span
            style={{
              fontSize: 40,
              fontWeight: 600,
              letterSpacing: 1,
              textShadow: "0 2px 12px rgba(255,255,255,0.9)",
            }}
          >
            {title}
          </span>
        ) : null}
        <span style={{ flex: 1 }} />
        {anchor ? (
          <span
            style={{
              fontFamily: FONT.mono,
              fontSize: 18,
              color: COLOR.inkMuted,
            }}
          >
            {anchor}
          </span>
        ) : null}
      </div>

      {hud}

      {/* 字幕条 */}
      <ShotCaptions captions={captions} />
    </AbsoluteFill>
  );
};

const ShotCaptions: React.FC<{ captions: Caption[] }> = ({ captions }) => {
  const frame = useCurrentFrame();
  if (captions.length === 0) return null;
  const current = [...captions].reverse().find((c) => frame >= c.from);
  if (!current) return null;

  const opacity = interpolate(frame - current.from, [0, 8], [0, 1], {
    extrapolateRight: "clamp",
  });

  return (
    <div
      style={{
        position: "absolute",
        left: 96,
        right: 96,
        bottom: 44,
        display: "flex",
        justifyContent: "center",
      }}
    >
      <div
        style={{
          opacity,
          fontSize: 30,
          lineHeight: 1.55,
          color: COLOR.ink,
          textAlign: "center",
          maxWidth: 1500,
          padding: "14px 36px",
          backgroundColor: "rgba(255, 255, 255, 0.9)",
          borderLeft: `4px solid ${COLOR.accent}`,
          borderRadius: 4,
          boxShadow: "0 4px 20px rgba(0,0,0,0.07)",
        }}
      >
        {current.text}
      </div>
    </div>
  );
};

/**
 * 核函数支撑球：真正画成球体，配合透明度与采样点表达权重分布。
 *
 * 这是平面图做不到的部分——支撑域是球，只有在三维里才能看出
 * 「半径 h 内才参与计算」的空间含义。
 */
export const KernelSupportSphere: React.FC<{
  frame: number;
  center?: [number, number, number];
  radius?: number;
  color?: string;
  showSamples?: boolean;
}> = ({
  frame,
  center = [0, 0, 0],
  radius = 2.2,
  color = COLOR.accent,
  showSamples = true,
}) => {
  const grow = interpolate(frame, [10, 55], [0.05, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
    easing: (t) => 1 - Math.pow(1 - t, 3),
  });

  // 球内按 Poly6 权重分布采样：越靠近球心权重越高，表面处为 0
  const samples = useMemo(() => {
    const rand = seeded(20260719);
    const pts: { pos: [number, number, number]; w: number }[] = [];
    for (let i = 0; i < 220; i++) {
      let x = 0;
      let y = 0;
      let z = 0;
      let d = 2;
      while (d > 1) {
        x = rand() * 2 - 1;
        y = rand() * 2 - 1;
        z = rand() * 2 - 1;
        d = x * x + y * y + z * z;
      }
      const r = Math.cbrt(rand());
      const w = Math.pow(Math.max(0, 1 - r * r), 3);
      pts.push({ pos: [x * r * radius, y * r * radius, z * r * radius], w });
    }
    return pts;
  }, [radius]);

  return (
    <group position={center}>
      <mesh scale={radius * grow}>
        <sphereGeometry args={[1, 48, 48]} />
        <meshStandardMaterial
          color={color}
          transparent
          opacity={0.13}
          roughness={0.9}
          depthWrite={false}
          side={THREE.DoubleSide}
        />
      </mesh>
      <mesh scale={radius * grow} rotation={[Math.PI / 2, 0, 0]}>
        <torusGeometry args={[1, 0.006, 8, 96]} />
        <meshStandardMaterial color={color} transparent opacity={0.5} />
      </mesh>
      <mesh scale={radius * grow} rotation={[0, Math.PI / 2, 0]}>
        <torusGeometry args={[1, 0.006, 8, 96]} />
        <meshStandardMaterial color={color} transparent opacity={0.35} />
      </mesh>

      {showSamples
        ? samples.map((s, i) => (
            <mesh
              key={i}
              position={[s.pos[0] * grow, s.pos[1] * grow, s.pos[2] * grow]}
            >
              <sphereGeometry args={[0.035 + s.w * 0.075, 8, 8]} />
              <meshStandardMaterial
                color={color}
                transparent
                opacity={0.25 + s.w * 0.75}
                emissive={color}
                emissiveIntensity={s.w * 0.5}
              />
            </mesh>
          ))
        : null}
    </group>
  );
};

/** 中心粒子 */
export const CenterParticle: React.FC<{
  position?: [number, number, number];
  color?: string;
  radius?: number;
}> = ({ position = [0, 0, 0], color = COLOR.orange, radius = 0.16 }) => (
  <mesh position={position}>
    <sphereGeometry args={[radius, 32, 32]} />
    <meshStandardMaterial color={color} roughness={0.3} metalness={0.15} />
  </mesh>
);

/** 地面 + 网格，给三维场景提供空间参照 */
export const Ground: React.FC<{ y?: number; size?: number }> = ({
  y = -1.9,
  size = 13,
}) => (
  <group>
    <mesh position={[0, y, 0]} rotation={[-Math.PI / 2, 0, 0]}>
      <planeGeometry args={[size, size]} />
      <meshStandardMaterial color="#DEDAD0" roughness={0.95} />
    </mesh>
    <gridHelper
      args={[size, size, "#BEBAAE", "#CFCBC1"]}
      position={[0, y + 0.01, 0]}
    />
  </group>
);
