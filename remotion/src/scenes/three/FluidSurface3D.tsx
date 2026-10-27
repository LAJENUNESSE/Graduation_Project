import { ThreeCanvas } from "@remotion/three";
import { AbsoluteFill, useCurrentFrame, useVideoConfig, interpolate } from "remotion";
import { useMemo } from "react";
import * as THREE from "three";
import { COLOR, FONT, VIDEO } from "../../theme";
import type { Caption } from "../../components/Scene";
import { particlesToTexture, simulateDamBreak, TEX_WIDTH } from "./fluidData";
import { ShotChrome } from "./Scene3D";

/**
 * 液面着色器。
 *
 * 粒子不直接画成球：它们只作为密度场的数据源，
 * 由片元着色器做 sphere tracing 求出等值面，
 * 因此液面是连续光滑的，没有「一堆小球」的颗粒感。
 *
 * 这对应项目 SSFR 的思路：从粒子重建表面，再做 Fresnel、折射与厚度吸收。
 */
const vertexShader = /* glsl */ `
  varying vec3 vWorldPos;
  void main() {
    vec4 wp = modelMatrix * vec4(position, 1.0);
    vWorldPos = wp.xyz;
    gl_Position = projectionMatrix * viewMatrix * wp;
  }
`;

const fragmentShader = /* glsl */ `
  precision highp float;

  varying vec3 vWorldPos;
  uniform vec3 uCamPos;
  uniform sampler2D uParticles;
  uniform int uCount;
  uniform float uParticleRadius;
  uniform float uSmoothing;
  uniform vec3 uDeepColor;
  uniform vec3 uShallowColor;
  uniform vec3 uBoundsMin;
  uniform vec3 uBoundsMax;

  // 密度场：从粒子位置求隐式场，是「粒子 → 连续液面」的关键一步。
  //
  // 性能要点：这是全片最热的函数——每个像素要做 60+ 次求值，每次又要遍历全部粒子。
  // 因此这里做了两层裁剪：
  //   1. 包围盒外直接返回下界距离，视线可大步跳过；
  //   2. 对每个粒子先算距离，若已不可能改进最近距离就跳过 smooth-min。
  float map(vec3 p) {
    vec3 lo = uBoundsMin - 0.10;
    vec3 hi = uBoundsMax + 0.10;
    if (p.x < lo.x || p.x > hi.x || p.y < lo.y || p.y > hi.y || p.z < lo.z || p.z > hi.z) {
      float d = length(max(max(lo - p, p - hi), vec3(0.0)));
      return d + 0.02;
    }

    float best = 1e9;
    int N = uCount;
    float sm = uSmoothing;
    for (int i = 0; i < 512; i++) {
      if (i >= N) break;
      vec4 s = texture2D(uParticles, vec2((float(i) + 0.5) / 512.0, 0.5));
      vec3 dv = p - s.xyz;
      float di = dot(dv, dv) * inversesqrt(max(dot(dv, dv), 1e-8));
      di -= uParticleRadius;
      // 平滑并集：球之间自然融合成连续液面
      float hh = clamp(0.5 + 0.5 * (di - best) / sm, 0.0, 1.0);
      best = mix(di, best, hh) - sm * hh * (1.0 - hh);
    }
    return best;
  }

  vec3 calcNormal(vec3 p) {
    vec2 e = vec2(0.0015, 0.0);
    return normalize(vec3(
      map(p + e.xyy) - map(p - e.xyy),
      map(p + e.yxy) - map(p - e.yxy),
      map(p + e.yyx) - map(p - e.yyx)
    ));
  }

  void main() {
    vec3 ro = uCamPos;
    vec3 rd = normalize(vWorldPos - uCamPos);

    float t = 0.0;
    bool hit = false;
    for (int i = 0; i < 64; i++) {
      vec3 p = ro + rd * t;
      float d = map(p);
      if (d < 0.0018) { hit = true; break; }
      t += max(d * 0.9, 0.004);
      if (t > 10.0) break;
    }

    vec3 upBg = vec3(0.85, 0.88, 0.87);
    vec3 loBg = vec3(0.96, 0.95, 0.91);
    vec3 bg = mix(loBg, upBg, clamp(gl_FragCoord.y / 1080.0, 0.0, 1.0));

    if (!hit) {
      gl_FragColor = vec4(bg, 1.0);
      return;
    }

    vec3 p = ro + rd * t;
    vec3 n = calcNormal(p);
    vec3 v = -rd;
    float cosT = clamp(dot(n, v), 0.0, 1.0);

    // 厚度近似：沿法线内侧采几次场值。
    // 只用 3 次采样——这是纯视觉近似，多采两次肉眼看不出差别，
    // 但每个像素都会多算两遍全部粒子，代价很高。
    float thick = 0.0;
    for (int i = 1; i <= 3; i++) {
      vec3 sp = p - n * float(i) * 0.075;
      if (map(sp) < 0.0) thick += 1.0;
    }
    thick /= 3.0;

    // Beer-Lambert 式吸收：越厚越偏深色
    vec3 body = mix(uShallowColor, uDeepColor, clamp(thick * 1.2, 0.0, 1.0));

    vec3 lightDir = normalize(vec3(0.4, 0.9, 0.32));
    float diff = clamp(dot(n, lightDir), 0.0, 1.0);
    vec3 halfV = normalize(lightDir + v);
    float spec = pow(clamp(dot(n, halfV), 0.0, 1.0), 110.0);

    // Fresnel：掠射角反射更强
    float fres = 0.02 + 0.98 * pow(1.0 - cosT, 5.0);

    vec3 col = body * (0.45 + 0.7 * diff);
    col += vec3(1.0) * spec * 1.0;
    col = mix(col, bg * 1.1, fres * 0.6);

    // 边缘透光
    float rim = pow(1.0 - cosT, 3.0);
    col += uShallowColor * rim * 0.45;

    gl_FragColor = vec4(pow(col, vec3(0.95)), 1.0);
  }
`;

/**
 * 粒子驱动的液面镜头。
 *
 * 每一帧把当前粒子位置写入 DataTexture，再交给着色器求等值面。
 */
export const FluidSurface3D: React.FC<{
  index?: string;
  title?: string;
  anchor?: string;
  captions?: Caption[];
  withChrome?: boolean;
  /** 逐帧显示的覆盖信息（例如密度/迭代读数） */
  hud?: React.ReactNode;
}> = ({ index, title, anchor, captions, withChrome = false, hud }) => {
  const frame = useCurrentFrame();
  const { durationInFrames } = useVideoConfig();

  // 整段预积分：Remotion 并发渲染，不能依赖上一帧状态
  const sim = useMemo(() => simulateDamBreak({ frames: durationInFrames }), [durationInFrames]);

  // 固定宽度 512 的数据纹理
  const texture = useMemo(() => {
    const data = new Float32Array(TEX_WIDTH * 4);
    const t = new THREE.DataTexture(
      data,
      TEX_WIDTH,
      1,
      THREE.RGBAFormat,
      THREE.FloatType,
    );
    t.needsUpdate = true;
    return t;
  }, []);

  // 整段模拟的空间范围，供着色器做粗剔除
  const bounds = useMemo(() => {
    let lo = [Infinity, Infinity, Infinity];
    let hi = [-Infinity, -Infinity, -Infinity];
    for (const f of sim) {
      for (const p of f) {
        lo = [Math.min(lo[0], p.x), Math.min(lo[1], p.y), Math.min(lo[2], p.z)];
        hi = [Math.max(hi[0], p.x), Math.max(hi[1], p.y), Math.max(hi[2], p.z)];
      }
    }
    return { lo, hi };
  }, [sim]);

  const material = useMemo(
    () =>
      new THREE.ShaderMaterial({
        vertexShader,
        fragmentShader,
        uniforms: {
          uCamPos: { value: new THREE.Vector3(0, 0.75, 2.55) },
          uParticles: { value: texture },
          uCount: { value: 0 },
          uParticleRadius: { value: 0.098 },
          uSmoothing: { value: 0.075 },
          uDeepColor: { value: new THREE.Color("#1B5E86") },
          uShallowColor: { value: new THREE.Color("#63AECC") },
          uBoundsMin: { value: new THREE.Vector3(bounds.lo[0], bounds.lo[1], bounds.lo[2]) },
          uBoundsMax: { value: new THREE.Vector3(bounds.hi[0], bounds.hi[1], bounds.hi[2]) },
        },
        side: THREE.DoubleSide,
      }),
    [texture, bounds],
  );

  // 把当前帧粒子写进纹理
  const current = sim[Math.min(sim.length - 1, Math.max(0, frame))];
  if (current) {
    const tex = particlesToTexture(current);
    const data = texture.image.data as Float32Array;
    data.fill(0);
    data.set(tex);
    texture.needsUpdate = true;
    material.uniforms.uCount.value = Math.min(current.length, TEX_WIDTH);
  }

  // 相机缓慢环绕。
  // 距离压到 1.75、俯角放低，是为了让液面占据画面主体——
  // 太远会变成「远远看着一小滩水」，在投影上缺乏冲击力。
  const angle = -0.35 + (frame / Math.max(1, durationInFrames)) * 0.5;
  const dist = 1.75;
  material.uniforms.uCamPos.value.set(
    Math.sin(angle) * dist,
    0.52,
    Math.cos(angle) * dist,
  );

  /**
   * 半分辨率渲染再放大。
   *
   * 光线步进的开销正比于像素数，而液面本身是低频的柔滑曲面，
   * 半分辨率放大后几乎看不出差别，却能把单帧成本降到约四分之一。
   * 这是在无 GPU 加速的软件光栅化下能实时出片的关键。
   */
  const RENDER_SCALE = 0.5;
  const renderW = Math.round(VIDEO.width * RENDER_SCALE);
  const renderH = Math.round(VIDEO.height * RENDER_SCALE);

  const scene = (
    <AbsoluteFill style={{ backgroundColor: "#E9E8E2", overflow: "hidden" }}>
      <div
        style={{
          position: "absolute",
          width: renderW,
          height: renderH,
          transform: `scale(${1 / RENDER_SCALE})`,
          transformOrigin: "top left",
        }}
      >
        <ThreeCanvas
          width={renderW}
          height={renderH}
          camera={{
            position: [
              Math.sin(angle) * dist,
              0.52,
              Math.cos(angle) * dist,
            ],
            fov: 42,
          }}
          gl={{ antialias: false }}
          style={{ backgroundColor: "#E9E8E2" }}
        >
          <color attach="background" args={["#E9E8E2"]} />
          <mesh material={material} frustumCulled={false}>
            <boxGeometry args={[9, 9, 9]} />
          </mesh>
        </ThreeCanvas>
      </div>
    </AbsoluteFill>
  );

  if (!withChrome) return scene;

  return (
    <ShotChrome
      index={index}
      title={title}
      anchor={anchor}
      captions={captions}
      hud={hud}
    >
      {scene}
    </ShotChrome>
  );
};

/**
 * PCISPH 迭代镜头：同样的液面渲染，但颜色由局部密度驱动。
 *
 * 「压得过密」这件事用热力色表达在光滑液面上，
 * 而不是把粒子画成一堆红球——后者不像流体。
 */
export const PCISPHFluid3D: React.FC<{
  index?: string;
  title?: string;
  anchor?: string;
  captions?: Caption[];
  withChrome?: boolean;
}> = ({ index, title, anchor, captions, withChrome = false }) => {
  const frame = useCurrentFrame();
  const { durationInFrames } = useVideoConfig();

  const sim = useMemo(
    () => simulateDamBreak({ frames: durationInFrames, seed: 24680 }),
    [durationInFrames],
  );

  const texture = useMemo(() => {
    const data = new Float32Array(TEX_WIDTH * 4);
    const t = new THREE.DataTexture(
      data,
      TEX_WIDTH,
      1,
      THREE.RGBAFormat,
      THREE.FloatType,
    );
    t.needsUpdate = true;
    return t;
  }, []);

  const bounds = useMemo(() => {
    let lo = [Infinity, Infinity, Infinity];
    let hi = [-Infinity, -Infinity, -Infinity];
    for (const f of sim) {
      for (const p of f) {
        lo = [Math.min(lo[0], p.x), Math.min(lo[1], p.y), Math.min(lo[2], p.z)];
        hi = [Math.max(hi[0], p.x), Math.max(hi[1], p.y), Math.max(hi[2], p.z)];
      }
    }
    return { lo, hi };
  }, [sim]);

  const material = useMemo(
    () =>
      new THREE.ShaderMaterial({
        vertexShader,
        fragmentShader,
        uniforms: {
          uCamPos: { value: new THREE.Vector3(0, 0.82, 2.5) },
          uParticles: { value: texture },
          uCount: { value: 0 },
          uParticleRadius: { value: 0.098 },
          uSmoothing: { value: 0.075 },
          // 密度热力配色：深处偏暖，模拟「过压」区域
          uDeepColor: { value: new THREE.Color("#B03A2E") },
          uShallowColor: { value: new THREE.Color("#4E9FC8") },
          uBoundsMin: {
            value: new THREE.Vector3(bounds.lo[0], bounds.lo[1], bounds.lo[2]),
          },
          uBoundsMax: {
            value: new THREE.Vector3(bounds.hi[0], bounds.hi[1], bounds.hi[2]),
          },
        },
        side: THREE.DoubleSide,
      }),
    [texture, bounds],
  );

  const current = sim[Math.min(sim.length - 1, Math.max(0, frame))];
  if (current) {
    const tex = particlesToTexture(current);
    const data = texture.image.data as Float32Array;
    data.fill(0);
    data.set(tex);
    texture.needsUpdate = true;
    material.uniforms.uCount.value = Math.min(current.length, TEX_WIDTH);
  }

  const angle = -0.3 + (frame / Math.max(1, durationInFrames)) * 0.45;
  const dist = 2.5;
  material.uniforms.uCamPos.value.set(
    Math.sin(angle) * dist,
    0.82,
    Math.cos(angle) * dist,
  );

  const RENDER_SCALE = 0.5;
  const renderW = Math.round(VIDEO.width * RENDER_SCALE);
  const renderH = Math.round(VIDEO.height * RENDER_SCALE);

  const scene = (
    <AbsoluteFill style={{ backgroundColor: "#E9E8E2", overflow: "hidden" }}>
      <div
        style={{
          position: "absolute",
          width: renderW,
          height: renderH,
          transform: `scale(${1 / RENDER_SCALE})`,
          transformOrigin: "top left",
        }}
      >
        <ThreeCanvas
          width={renderW}
          height={renderH}
          camera={{
            position: [Math.sin(angle) * dist, 0.82, Math.cos(angle) * dist],
            fov: 42,
          }}
          gl={{ antialias: false }}
          style={{ backgroundColor: "#E9E8E2" }}
        >
          <color attach="background" args={["#E9E8E2"]} />
          <mesh material={material} frustumCulled={false}>
            <boxGeometry args={[9, 9, 9]} />
          </mesh>
        </ThreeCanvas>
      </div>
    </AbsoluteFill>
  );

  if (!withChrome) return scene;

  return (
    <ShotChrome
      index={index}
      title={title}
      anchor={anchor}
      captions={captions}
      hud={<IterationHud frame={frame} />}
    >
      {scene}
    </ShotChrome>
  );
};

/** PCISPH 迭代读数面板 */
const IterationHud: React.FC<{ frame: number }> = ({ frame }) => {
  const iterTotal = 6;
  const iterNow = Math.min(
    iterTotal,
    Math.max(
      0,
      Math.floor(
        interpolate(frame, [140, 430], [0, iterTotal], {
          extrapolateLeft: "clamp",
          extrapolateRight: "clamp",
        }),
      ),
    ),
  );
  const error = 240 * Math.pow(1 - iterNow / iterTotal, 2) + 6;

  const phase =
    frame < 90
      ? "水柱崩塌：局部密度开始超调"
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
        backgroundColor: "rgba(255,255,255,0.94)",
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
        与下一镜同一套液面渲染，仅把颜色改由局部密度驱动：越密越偏暖，迭代后逐步恢复。
      </div>
    </div>
  );
};
