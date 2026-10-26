import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { COLOR, FONT } from "../theme";
import { Card } from "../components/Common";

/**
 * 真实实验结果，取自 benchmark/results/run_20260901_000119/summary.csv。
 * 数值不在页面里手工改写，保持与 CSV 一致，答辩被追问时能直接对上。
 */
const SIZES = [1000, 5000, 10000, 20000, 50000];

const PCISPH = [
  { backend: "OpenGL", values: [2.21, 14.49, 19.22, 30.94, 90.72], color: COLOR.accent },
  { backend: "CUDA", values: [1.81, 10.51, 14.32, 24.61, 75.79], color: COLOR.green },
];

const SPEEDUP_WCSPH = [1.01, 1.07, 1.10, 0.99, 1.55];

/** 11 实测性能与一致性 */
export const Results: React.FC = () => {
  const frame = useCurrentFrame();
  const grow = interpolate(frame, [20, 90], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
    easing: (t) => 1 - Math.pow(1 - t, 3),
  });
  const inB = interpolate(frame, [100, 135], [0, 1], { extrapolateRight: "clamp" });

  const maxVal = 100;
  const chartH = 400;

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bg,
        fontFamily: FONT.sans,
        flexDirection: "column",
        justifyContent: "center",
        gap: 26,
      }}
    >
      <div style={{ display: "flex", gap: 46, alignItems: "flex-end", justifyContent: "center" }}>
        {/* 左：PCISPH 单帧耗时 */}
        <div style={{ display: "flex", flexDirection: "column", gap: 14 }}>
          <div style={{ fontSize: 25, fontWeight: 700, color: COLOR.ink }}>
            PCISPH 单帧求解耗时（ms，越低越好）
          </div>
          <svg width={700} height={chartH + 80} style={{ overflow: "visible" }}>
            {/* 网格线 */}
            {[0, 25, 50, 75, 100].map((v) => {
              const y = chartH - (v / maxVal) * chartH + 20;
              return (
                <g key={v}>
                  <line x1={54} y1={y} x2={660} y2={y} stroke={COLOR.rule} strokeWidth={1} strokeDasharray="4 6" />
                  <text x={44} y={y + 6} fontSize={16} fill={COLOR.inkMuted} textAnchor="end">
                    {v}
                  </text>
                </g>
              );
            })}
            {/* 柱 */}
            {SIZES.map((size, i) => {
              const groupX = 80 + i * 118;
              return (
                <g key={size}>
                  {PCISPH.map((series, si) => {
                    const v = series.values[i];
                    const h = (v / maxVal) * chartH * grow;
                    const y = chartH + 20 - h;
                    return (
                      <g key={series.backend}>
                        <rect
                          x={groupX + si * 30}
                          y={y}
                          width={26}
                          height={h}
                          fill={series.color}
                          rx={2}
                        />
                        <text
                          x={groupX + si * 30 + 13}
                          y={y - 6}
                          fontSize={14}
                          fill={COLOR.inkSoft}
                          textAnchor="middle"
                          opacity={grow}
                        >
                          {v.toFixed(1)}
                        </text>
                      </g>
                    );
                  })}
                  <text x={groupX + 42} y={chartH + 44} fontSize={16} fill={COLOR.inkSoft} textAnchor="middle">
                    {size >= 1000 ? `${size / 1000}k` : size}
                  </text>
                </g>
              );
            })}
          </svg>
          <div style={{ display: "flex", gap: 22, justifyContent: "center", marginTop: 20 }}>
            {PCISPH.map((s) => (
              <div key={s.backend} style={{ display: "flex", alignItems: "center", gap: 8, fontSize: 19 }}>
                <span style={{ width: 15, height: 15, backgroundColor: s.color, borderRadius: 2, display: "inline-block" }} />
                {s.backend}
              </div>
            ))}
          </div>
        </div>

        {/* 右：WCSPH 加速比 */}
        <div style={{ display: "flex", flexDirection: "column", gap: 14, opacity: inB }}>
          <div style={{ fontSize: 25, fontWeight: 700, color: COLOR.ink }}>
            WCSPH 加速比（CUDA / OpenGL）
          </div>
          <div style={{ display: "flex", flexDirection: "column", gap: 12 }}>
            {SIZES.map((size, i) => {
              const v = SPEEDUP_WCSPH[i];
              const t = interpolate(frame, [110 + i * 12, 130 + i * 12], [0, 1], {
                extrapolateLeft: "clamp",
                extrapolateRight: "clamp",
              });
              const isDip = v < 1;
              return (
                <div key={size} style={{ display: "flex", alignItems: "center", gap: 14, opacity: t }}>
                  <span style={{ width: 62, fontSize: 19, color: COLOR.inkSoft, textAlign: "right" }}>
                    {size / 1000}k
                  </span>
                  <div style={{ width: 300, height: 26, backgroundColor: COLOR.bgAlt, borderRadius: 3, position: "relative" }}>
                    <div
                      style={{
                        width: `${(v / 1.7) * 100}%`,
                        height: "100%",
                        backgroundColor: isDip ? COLOR.orange : COLOR.green,
                        borderRadius: 3,
                      }}
                    />
                    {/* 1.0 参考线 */}
                    <div
                      style={{
                        position: "absolute",
                        left: `${(1 / 1.7) * 100}%`,
                        top: -4,
                        bottom: -4,
                        width: 2,
                        backgroundColor: COLOR.ink,
                      }}
                    />
                  </div>
                  <span
                    style={{
                      fontFamily: FONT.serif,
                      fontSize: 23,
                      fontWeight: 700,
                      color: isDip ? COLOR.orange : COLOR.green,
                    }}
                  >
                    {v.toFixed(2)}×
                  </span>
                </div>
              );
            })}
          </div>
          <div style={{ fontSize: 18, color: COLOR.inkMuted, lineHeight: 1.6, marginTop: 4 }}>
            20k 档 0.99× 与基线持平：WCSPH 属轻负载，该粒子区间 OpenGL 基线
            由固定开销主导转入计算主导，两条曲线在此交叉；50k 档升至 1.55×。
          </div>
        </div>
      </div>

      <div style={{ display: "flex", gap: 24, marginTop: 8 }}>
        <Card title="实验规模" color={COLOR.accent} soft={COLOR.accentSoft}>
          <span style={{ fontSize: 20 }}>
            3 后端 × 2 求解器 × 5 粒子规模 = 30 组对照，45.5 万条逐帧样本。
          </span>
        </Card>
        <Card title="正确性先于性能" color={COLOR.green} soft={COLOR.greenSoft}>
          <span style={{ fontSize: 20 }}>
            冻结密度偏差容差做门禁，跨后端平均密度最大相对偏差 0.0261%，
            先证明三后端算的是同一套物理，再比较快慢。
          </span>
        </Card>
      </div>
    </AbsoluteFill>
  );
};

/** 12 结尾：一句话总结 */
export const Outro: React.FC = () => {
  const frame = useCurrentFrame();
  const a = interpolate(frame, [0, 26], [0, 1], { extrapolateRight: "clamp" });
  const b = interpolate(frame, [40, 70], [0, 1], { extrapolateRight: "clamp" });
  const c = interpolate(frame, [90, 120], [0, 1], { extrapolateRight: "clamp" });

  const chain = [
    "离散成粒子",
    "核函数加权",
    "密度估计",
    "状态方程给压力",
    "PCISPH 迭代校正",
    "GPU 并行执行",
  ];

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bgAlt,
        fontFamily: FONT.sans,
        justifyContent: "center",
        alignItems: "center",
        flexDirection: "column",
        gap: 46,
      }}
    >
      <div style={{ opacity: a, fontSize: 40, fontWeight: 700, color: COLOR.ink }}>
        一条完整链路
      </div>

      <div style={{ display: "flex", alignItems: "center", gap: 12, opacity: b }}>
        {chain.map((s, i) => (
          <div key={s} style={{ display: "flex", alignItems: "center", gap: 12 }}>
            <span
              style={{
                padding: "14px 22px",
                backgroundColor: "#FFFFFF",
                border: `2px solid ${COLOR.accent}`,
                borderRadius: 6,
                fontSize: 21,
                color: COLOR.ink,
              }}
            >
              {s}
            </span>
            {i < chain.length - 1 ? (
              <span style={{ color: COLOR.accent, fontSize: 24 }}>→</span>
            ) : null}
          </div>
        ))}
      </div>

      <div
        style={{
          opacity: c,
          maxWidth: 1240,
          textAlign: "center",
          fontSize: 25,
          lineHeight: 1.9,
          color: COLOR.inkSoft,
        }}
      >
        亮点不只是「实现了流体」，而是把经典粒子流体的数学模型
        落实成一条完整的 GPU 管线，并围绕真实工程问题补上了
        warm-up、加速度限幅、预测位置重建 grid、刚体 SDF 耦合、
        CPU 侧系数预计算与 CUDA/GL 双后端一致性。
      </div>
    </AbsoluteFill>
  );
};
