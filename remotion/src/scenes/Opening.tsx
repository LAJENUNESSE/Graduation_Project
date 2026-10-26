import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { COLOR, FONT, PAD } from "../theme";
import { Math as MathBlock, Sub, V, Vec, Frac, Sum } from "../components/Math";

/** 00 片头 */
export const Intro: React.FC = () => {
  const frame = useCurrentFrame();
  const titleIn = interpolate(frame, [0, 24], [0, 1], {
    extrapolateRight: "clamp",
    easing: (t) => 1 - Math.pow(1 - t, 3),
  });
  const subIn = interpolate(frame, [18, 42], [0, 1], {
    extrapolateRight: "clamp",
  });
  const lineWidth = interpolate(frame, [10, 40], [0, 420], {
    extrapolateRight: "clamp",
    easing: (t) => 1 - Math.pow(1 - t, 3),
  });

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bg,
        fontFamily: FONT.sans,
        justifyContent: "center",
        alignItems: "center",
      }}
    >
      <div style={{ opacity: titleIn, transform: `translateY(${(1 - titleIn) * 24}px)` }}>
        <div
          style={{
            fontSize: 74,
            fontWeight: 700,
            letterSpacing: 6,
            color: COLOR.ink,
            textAlign: "center",
          }}
        >
          SPH 流体模拟原理
        </div>
      </div>

      <div
        style={{
          height: 4,
          width: lineWidth,
          backgroundColor: COLOR.accent,
          margin: "30px 0",
          borderRadius: 2,
        }}
      />

      <div
        style={{
          opacity: subIn,
          fontSize: 30,
          color: COLOR.inkSoft,
          letterSpacing: 3,
          textAlign: "center",
        }}
      >
        从拉格朗日离散到 GPU 上的 PCISPH 预测-校正管线
      </div>

      <div
        style={{
          opacity: interpolate(frame, [46, 70], [0, 1], { extrapolateRight: "clamp" }),
          marginTop: 70,
          fontFamily: FONT.mono,
          fontSize: 21,
          color: COLOR.inkMuted,
          textAlign: "center",
          lineHeight: 2,
        }}
      >
        WCSPH / PCISPH · OpenGL Compute · CUDA · 三后端数值一致性
      </div>

      {/* 右下角源码锚点，提示这部片子讲的是仓库里真实存在的实现 */}
      <div
        style={{
          position: "absolute",
          bottom: PAD * 0.6,
          right: PAD,
          opacity: interpolate(frame, [60, 84], [0, 1], { extrapolateRight: "clamp" }),
          fontFamily: FONT.mono,
          fontSize: 18,
          color: COLOR.inkMuted,
          textAlign: "right",
          lineHeight: 1.8,
        }}
      >
        Engine/src/Renderer/SPHKernelMath.h
        <br />
        assets/shaders/sph_*.glsl
        <br />
        Engine/Platform/CUDA/CudaSPHPipeline.h
      </div>
    </AbsoluteFill>
  );
};

/** 01 拉格朗日离散：为什么不划网格 */
export const Discretization: React.FC = () => {
  const frame = useCurrentFrame();
  // 网格线逐渐淡出，让“放弃网格”这件事在画面上真的发生
  const gridFade = interpolate(frame, [70, 130], [0.55, 0], {
    extrapolateRight: "clamp",
  });
  const particleIn = interpolate(frame, [90, 150], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });

  const size = 420;
  const cols = 9;
  const gap = size / cols;

  // 固定种子的粒子分布，避免逐帧随机导致抖动
  const particles = [
    [1, 2], [2, 1], [2, 3], [3, 2], [3, 4], [4, 1], [4, 3], [5, 2], [5, 5],
    [6, 1], [6, 4], [7, 2], [7, 3], [1, 5], [2, 6], [3, 6], [4, 6], [5, 7],
    [6, 6], [7, 6], [8, 2], [8, 4], [2, 8], [4, 8], [6, 8], [7, 8], [3, 7],
    [5, 8], [8, 6], [1, 7],
  ];

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bg,
        fontFamily: FONT.sans,
        justifyContent: "center",
        alignItems: "center",
        flexDirection: "column",
        gap: 46,
      }}
    >
      <div
        style={{
          position: "relative",
          width: size,
          height: size,
          border: `2px solid ${COLOR.rule}`,
        }}
      >
        {/* 欧拉网格 */}
        {Array.from({ length: cols + 1 }).map((_, i) => (
          <div
            key={`v${i}`}
            style={{
              position: "absolute",
              left: i * gap,
              top: 0,
              width: 1,
              height: size,
              backgroundColor: COLOR.accent,
              opacity: gridFade,
            }}
          />
        ))}
        {Array.from({ length: cols + 1 }).map((_, i) => (
          <div
            key={`h${i}`}
            style={{
              position: "absolute",
              top: i * gap,
              left: 0,
              height: 1,
              width: size,
              backgroundColor: COLOR.accent,
              opacity: gridFade,
            }}
          />
        ))}

        {/* 拉格朗日粒子 */}
        {particles.map(([px, py], i) => (
          <div
            key={i}
            style={{
              position: "absolute",
              left: px * gap - 8,
              top: size - py * gap - 8,
              width: 16,
              height: 16,
              borderRadius: "50%",
              backgroundColor: COLOR.orange,
              opacity: particleIn,
              transform: `scale(${0.4 + particleIn * 0.6})`,
            }}
          />
        ))}
      </div>

      <div style={{ display: "flex", gap: 50, alignItems: "center" }}>
        <div style={{ fontSize: 26, color: COLOR.accent, opacity: gridFade * 1.8 }}>
          欧拉网格：场量绑在格点上
        </div>
        <div style={{ fontSize: 26, color: COLOR.orange, opacity: particleIn }}>
          拉格朗日粒子：场量绑在粒子上
        </div>
      </div>
    </AbsoluteFill>
  );
};

/** 02 SPH 母式：任意场量的核近似 */
export const MotherFormula: React.FC = () => {
  const frame = useCurrentFrame();
  const inA = interpolate(frame, [0, 20], [0, 1], { extrapolateRight: "clamp" });
  const inB = interpolate(frame, [40, 64], [0, 1], { extrapolateRight: "clamp" });
  const inC = interpolate(frame, [96, 126], [0, 1], { extrapolateRight: "clamp" });

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bgAlt,
        fontFamily: FONT.sans,
        justifyContent: "center",
        alignItems: "center",
        flexDirection: "column",
        gap: 54,
      }}
    >
      <div style={{ opacity: inA, display: "flex", justifyContent: "center" }}>
        <MathBlock size={56}>
          <V>A</V>(<Vec>x</Vec>) ≈{" "}
          <Sum from={<V>j</V>} />
          <V>m</V>
          <Sub>j</Sub>
          <Frac
            num={
              <>
                <V>A</V>
                <Sub>j</Sub>
              </>
            }
            den={
              <>
                <V>ρ</V>
                <Sub>j</Sub>
              </>
            }
          />
          <V>W</V>(<Vec>x</Vec> − <Vec>x</Vec>
          <Sub>j</Sub>, <V>h</V>)
        </MathBlock>
      </div>

      <div
        style={{
          opacity: inB,
          display: "flex",
          gap: 60,
          justifyContent: "center",
          fontSize: 24,
          color: COLOR.inkSoft,
        }}
      >
        <span>
          <V>m</V>
          <Sub>j</Sub> 粒子质量
        </span>
        <span>
          <V>ρ</V>
          <Sub>j</Sub> 粒子密度
        </span>
        <span>
          <V>W</V> 光滑核函数
        </span>
        <span>
          <V>h</V> 光滑半径（支撑域）
        </span>
      </div>

      <div
        style={{
          opacity: inC,
          maxWidth: 1180,
          textAlign: "center",
          fontSize: 26,
          lineHeight: 1.8,
          color: COLOR.ink,
          borderTop: `2px solid ${COLOR.rule}`,
          paddingTop: 30,
        }}
      >
        连续场被换成了「邻域内有限个粒子的加权和」——
        这既是 SPH 的全部起点，也是它所有并行性的来源：每个粒子的计算互相独立。
      </div>
    </AbsoluteFill>
  );
};
