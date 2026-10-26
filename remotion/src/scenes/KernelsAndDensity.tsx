import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { COLOR, FONT } from "../theme";
import { Frac, Math as MathBlock, Sub, Sup, V } from "../components/Math";
import { Card } from "../components/Common";

/** 03 核函数：Poly6 与 Spiky 的分工 */
export const Kernels: React.FC = () => {
  const frame = useCurrentFrame();
  const draw = interpolate(frame, [20, 90], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });

  const W = 640;
  const H = 300;
  const pad = 46;

  // Poly6 形状：(1 - (r/h)^2)^3，在 r=0 处平滑取到最大值
  const poly6 = (r: number) => Math.pow(Math.max(0, 1 - r * r), 3);
  // Spiky 梯度形状：-(1 - r/h)^2，在 r→0 处陡峭，短程排斥更强
  const spikyGrad = (r: number) => -Math.pow(Math.max(0, 1 - r), 2);

  const toPath = (fn: (r: number) => number, vMin: number, vMax: number) => {
    const pts: string[] = [];
    const steps = 120;
    const drawn = Math.floor(steps * draw);
    for (let i = 0; i <= drawn; i++) {
      const r = i / steps;
      const x = pad + r * (W - pad * 2);
      const v = fn(r);
      const norm = (v - vMin) / (vMax - vMin);
      const y = H - pad - norm * (H - pad * 2);
      pts.push(`${i === 0 ? "M" : "L"}${x.toFixed(1)},${y.toFixed(1)}`);
    }
    return pts.join(" ");
  };

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bg,
        fontFamily: FONT.sans,
        flexDirection: "column",
        justifyContent: "center",
        gap: 34,
      }}
    >
      <div style={{ display: "flex", gap: 70, justifyContent: "center" }}>
        {/* Poly6 */}
        <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 16 }}>
          <div style={{ fontSize: 27, fontWeight: 700, color: COLOR.accent }}>
            Poly6 —— 用于密度估计
          </div>
          <svg width={W} height={H} style={{ overflow: "visible" }}>
            <line x1={pad} y1={H - pad} x2={W - pad} y2={H - pad} stroke={COLOR.rule} strokeWidth={2} />
            <line x1={pad} y1={pad * 0.5} x2={pad} y2={H - pad} stroke={COLOR.rule} strokeWidth={2} />
            <path d={toPath(poly6, 0, 1.08)} fill="none" stroke={COLOR.accent} strokeWidth={4} />
            <text x={W / 2} y={H - 8} fill={COLOR.inkMuted} fontSize={19} textAnchor="middle">
              距离 r（0 → h）
            </text>
          </svg>
          <div style={{ fontFamily: FONT.serif, fontSize: 30, color: COLOR.ink }}>
            <V>W</V>
            <Sub>poly6</Sub>(<V>r</V>,<V>h</V>) ={" "}
            <Frac
              num="315"
              den={
                <>
                  64π<V>h</V>
                  <Sup>9</Sup>
                </>
              }
            />
            (<V>h</V>
            <Sup>2</Sup> − <V>r</V>
            <Sup>2</Sup>)
            <Sup>3</Sup>
          </div>
          <div style={{ fontSize: 21, color: COLOR.inkSoft, textAlign: "center", maxWidth: 600 }}>
            r=0 附近平滑、支撑域有限，密度求和不会因邻粒子贴近而剧烈跳变
          </div>
        </div>

        {/* Spiky 梯度 */}
        <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 16 }}>
          <div style={{ fontSize: 27, fontWeight: 700, color: COLOR.orange }}>
            Spiky 梯度 —— 用于压力与表面法线
          </div>
          <svg width={W} height={H} style={{ overflow: "visible" }}>
            <line x1={pad} y1={H - pad} x2={W - pad} y2={H - pad} stroke={COLOR.rule} strokeWidth={2} />
            <line x1={pad} y1={pad * 0.5} x2={pad} y2={H - pad} stroke={COLOR.rule} strokeWidth={2} />
            <path d={toPath(spikyGrad, -1.15, 0.15)} fill="none" stroke={COLOR.orange} strokeWidth={4} />
            <text x={W / 2} y={H - 8} fill={COLOR.inkMuted} fontSize={19} textAnchor="middle">
              距离 r（0 → h）
            </text>
          </svg>
          <div style={{ fontFamily: FONT.serif, fontSize: 30, color: COLOR.ink }}>
            ∇<V>W</V>
            <Sub>spiky</Sub>(<V>r</V>,<V>h</V>) = −
            <Frac
              num="45"
              den={
                <>
                  π<V>h</V>
                  <Sup>6</Sup>
                </>
              }
            />
            (<V>h</V>−<V>r</V>)
            <Sup>2</Sup>
            <span style={{ fontStyle: "italic" }}> r̂</span>
          </div>
          <div style={{ fontSize: 21, color: COLOR.inkSoft, textAlign: "center", maxWidth: 600 }}>
            梯度在 r→0 处最陡，近距离排斥力足够强，粒子不会互相穿插
          </div>
        </div>
      </div>

      <div
        style={{
          opacity: interpolate(frame, [96, 130], [0, 1], { extrapolateRight: "clamp" }),
          display: "flex",
          justifyContent: "center",
        }}
      >
        <div
          style={{
            fontFamily: FONT.mono,
            fontSize: 21,
            color: COLOR.inkMuted,
            backgroundColor: COLOR.bgAlt,
            padding: "12px 26px",
            borderRadius: 6,
          }}
        >
          SPHKernelMath.h：poly6Coeff = 315/(64πh⁹)，spikyCoeff = −45/(πh⁶) —— 两项系数在 CPU 侧预计算，GPU 每个邻居只做一次乘法
        </div>
      </div>
    </AbsoluteFill>
  );
};

/** 04 密度估计与状态方程 */
export const DensityAndEOS: React.FC = () => {
  const frame = useCurrentFrame();
  const inA = interpolate(frame, [0, 22], [0, 1], { extrapolateRight: "clamp" });
  const inB = interpolate(frame, [56, 84], [0, 1], { extrapolateRight: "clamp" });
  const inC = interpolate(frame, [126, 156], [0, 1], { extrapolateRight: "clamp" });

  // 密度误差随帧累积，用来演示 P 的触发
  const sampleRho = interpolate(frame, [70, 150], [1000, 1240], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });
  const rho0 = 1000;
  const k = 50;
  const pressure = Math.max(0, k * (sampleRho - rho0));

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bgAlt,
        fontFamily: FONT.sans,
        flexDirection: "column",
        justifyContent: "center",
        alignItems: "center",
        gap: 36,
      }}
    >
      <div style={{ opacity: inA, display: "flex", justifyContent: "center" }}>
        <MathBlock size={44}>
          <V>ρ</V>
          <Sub>i</Sub> = Σ<Sub>j</Sub> <V>m</V>
          <Sub>j</Sub> <V>W</V>
          <Sub>poly6</Sub>(<V>r</V>
          <Sub>ij</Sub>, <V>h</V>)
        </MathBlock>
      </div>

      <div style={{ opacity: inB, display: "flex", justifyContent: "center" }}>
        <MathBlock size={44}>
          <V>P</V>
          <Sub>i</Sub> = max(0, <V>k</V>(<V>ρ</V>
          <Sub>i</Sub> − <V>ρ</V>
          <Sub>0</Sub>))
        </MathBlock>
      </div>

      {/* 实时读数：把公式和具体数值对上，答辩时最容易讲清 */}
      <div
        style={{
          opacity: inB,
          display: "flex",
          gap: 30,
          alignItems: "center",
          marginTop: 6,
        }}
      >
        <Readout label="当前密度 ρ" value={sampleRho.toFixed(0)} unit="" color={COLOR.accent} />
        <Readout label="静止密度 ρ₀" value={String(rho0)} unit="" color={COLOR.inkMuted} />
        <Readout label="气体常数 k" value={String(k)} unit="" color={COLOR.inkMuted} />
        <Readout label="压力 P" value={pressure.toFixed(0)} unit="" color={pressure > 0 ? COLOR.red : COLOR.green} />
      </div>

      <div
        style={{
          opacity: inC,
          display: "flex",
          gap: 28,
          width: 1560,
        }}
      >
        <Card title="为什么用 Poly6 算密度" color={COLOR.accent} soft={COLOR.accentSoft}>
          r=0 附近平滑、支撑域有限。若换成尖峰型核，邻粒子稍有位移密度就剧变，
          压力随之剧烈振荡。
        </Card>
        <Card title="为什么压力要 clamp 到非负" color={COLOR.orange} soft={COLOR.orangeSoft}>
          允许明显负压力会让粒子互相吸引，出现粘团、空洞塌陷和高频振荡。
          当前实现只保留压缩态的正压力，是更保守的工程化选择。
        </Card>
      </div>

      <div
        style={{
          opacity: inC,
          fontFamily: FONT.mono,
          fontSize: 20,
          color: COLOR.inkMuted,
        }}
      >
        assets/shaders/sph_density.glsl：pressure = max(0.0, u_GasConstant * (density - u_RestDensity))
      </div>
    </AbsoluteFill>
  );
};

const Readout: React.FC<{
  label: string;
  value: string;
  unit: string;
  color: string;
}> = ({ label, value, unit, color }) => (
  <div
    style={{
      display: "flex",
      flexDirection: "column",
      alignItems: "center",
      gap: 4,
      padding: "14px 30px",
      backgroundColor: "#FFFFFF",
      border: `2px solid ${COLOR.rule}`,
      borderRadius: 8,
      minWidth: 200,
    }}
  >
    <span style={{ fontSize: 19, color: COLOR.inkMuted }}>{label}</span>
    <span style={{ fontFamily: FONT.serif, fontSize: 38, color, fontWeight: 700 }}>
      {value}
      <span style={{ fontSize: 22, color: COLOR.inkMuted, marginLeft: 4 }}>{unit}</span>
    </span>
  </div>
);
