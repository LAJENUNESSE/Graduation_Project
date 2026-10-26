import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { COLOR, FONT } from "../theme";
import { Frac, Math as MathBlock, Sub, Sup, V, Vec } from "../components/Math";
import { pulse } from "../components/Common";

/** 05 WCSPH 力模型：压力 / 粘性 / 表面张力 / 边界 */
export const ForceModel: React.FC = () => {
  const frame = useCurrentFrame();
  const inA = interpolate(frame, [0, 20], [0, 1], { extrapolateRight: "clamp" });
  const inB = interpolate(frame, [50, 76], [0, 1], { extrapolateRight: "clamp" });
  const inC = interpolate(frame, [104, 132], [0, 1], { extrapolateRight: "clamp" });
  const inD = interpolate(frame, [162, 190], [0, 1], { extrapolateRight: "clamp" });

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bg,
        fontFamily: FONT.sans,
        flexDirection: "column",
        justifyContent: "center",
        gap: 30,
      }}
    >
      <FormulaRow
        opacity={inA}
        label="压力力（对称化形式）"
        color={COLOR.accent}
        formula={
          <MathBlock size={34}>
            <Vec>f</Vec>
            <Sup>pressure</Sup>
            <Sub>i</Sub> = − Σ<Sub>j</Sub> <V>m</V>
            <Sub>j</Sub>
            <Frac
              num={
                <>
                  <V>P</V>
                  <Sub>i</Sub> + <V>P</V>
                  <Sub>j</Sub>
                </>
              }
              den={
                <>
                  2<V>ρ</V>
                  <Sub>j</Sub>
                </>
              }
            />
            ∇<V>W</V>
            <Sub>spiky</Sub>
          </MathBlock>
        }
        note="(Pi+Pj)/2 是对称化处理，避免单边压力带来的偏置"
      />

      <FormulaRow
        opacity={inB}
        label="粘性力"
        color={COLOR.green}
        formula={
          <MathBlock size={34}>
            <Vec>f</Vec>
            <Sup>visc</Sup>
            <Sub>i</Sub> = <V>μ</V> Σ<Sub>j</Sub> <V>m</V>
            <Sub>j</Sub>
            <Frac
              num={
                <>
                  <Vec>v</Vec>
                  <Sub>j</Sub> − <Vec>v</Vec>
                  <Sub>i</Sub>
                </>
              }
              den={
                <>
                  <V>ρ</V>
                  <Sub>j</Sub>
                </>
              }
            />
            ∇<Sup>2</Sup>
            <V>W</V>
            <Sub>visc</Sub>
          </MathBlock>
        }
        note="抑制速度噪声，提高局部速度场的平滑性"
      />

      <FormulaRow
        opacity={inC}
        label="表面张力（Akinci 模型）"
        color={COLOR.purple}
        formula={
          <MathBlock size={34}>
            <Vec>n</Vec>
            <Sub>i</Sub> = <V>h</V> Σ<Sub>j</Sub>
            <Frac
              num={
                <>
                  <V>m</V>
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
            ∇<V>W</V>
            <Sub>spiky</Sub> ⇒ cohesion + curvature
          </MathBlock>
        }
        note="法线在 density pass 先算好，force pass 直接读取，避免重复遍历邻域"
      />

      <FormulaRow
        opacity={inD}
        label="刚体边界（SDF + 惩罚力）"
        color={COLOR.orange}
        formula={
          <MathBlock size={34}>
            <Vec>f</Vec>
            <Sub>boundary</Sub> = <V>k</V>
            <Sub>b</Sub>
            <V>d</V>
            <Vec>n</Vec> − <V>c</V>
            <Sub>b</Sub>(<Vec>v</Vec>
            <Sub>rel</Sub>·<Vec>n</Vec>)<Vec>n</Vec>
          </MathBlock>
        }
        note="惩罚式边界而非硬约束：d 是穿透深度，kb/ cb 对应 BoundaryStiffness / BoundaryDamping"
      />

      <div
        style={{
          opacity: interpolate(frame, [196, 224], [0, 1], { extrapolateRight: "clamp" }),
          textAlign: "center",
          fontFamily: FONT.mono,
          fontSize: 20,
          color: COLOR.inkMuted,
        }}
      >
        assets/shaders/sph_force.glsl（403 行）—— 同时处理 box / sphere 刚体与 mesh SDF 边界
      </div>
    </AbsoluteFill>
  );
};

const FormulaRow: React.FC<{
  opacity: number;
  label: string;
  color: string;
  formula: React.ReactNode;
  note: string;
}> = ({ opacity, label, color, formula, note }) => (
  <div
    style={{
      opacity,
      display: "flex",
      alignItems: "center",
      gap: 26,
      paddingLeft: 8,
      borderLeft: `5px solid ${color}`,
    }}
  >
    <div style={{ width: 330, fontSize: 24, fontWeight: 700, color }}>{label}</div>
    <div>
      {formula}
      <div style={{ fontSize: 19, color: COLOR.inkSoft, marginTop: 2 }}>{note}</div>
    </div>
  </div>
);

/** 06 空间哈希网格邻居搜索：cellSize = 2h，两段式前缀和 */
export const NeighborSearch: React.FC = () => {
  const frame = useCurrentFrame();
  const gridSize = 8;
  const cell = 92;
  const board = gridSize * cell;

  // 粒子按固定种子分布，保证逐帧确定性
  const particles: [number, number][] = [
    [1.4, 1.2], [2.2, 1.9], [3.1, 1.3], [3.6, 2.4], [1.7, 3.1],
    [2.6, 3.6], [4.2, 2.1], [4.9, 3.2], [5.4, 1.6], [5.9, 2.6],
    [1.2, 4.5], [2.4, 5.1], [3.3, 4.3], [4.4, 5.4], [5.4, 4.6],
    [6.3, 3.7], [6.6, 5.2], [2.9, 6.3], [4.6, 6.6], [5.9, 6.1],
  ];

  // 高亮的查询粒子
  const qx = 3.6;
  const qy = 2.4;
  const highlightR = interpolate(frame, [30, 90], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });
  // 3x3 邻域格子逐格亮起
  const cellLit = (cx: number, cy: number) => {
    const order = (cy - 1) * 3 + (cx - 1);
    return interpolate(frame, [90 + order * 6, 108 + order * 6], [0, 1], {
      extrapolateLeft: "clamp",
      extrapolateRight: "clamp",
    });
  };

  const qcx = Math.floor(qx);
  const qcy = Math.floor(qy);

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bgAlt,
        fontFamily: FONT.sans,
        flexDirection: "row",
        alignItems: "center",
        justifyContent: "center",
        gap: 70,
      }}
    >
      <svg width={board} height={board} style={{ flexShrink: 0 }}>
        {/* 网格 */}
        {Array.from({ length: gridSize + 1 }).map((_, i) => (
          <g key={i}>
            <line x1={i * cell} y1={0} x2={i * cell} y2={board} stroke={COLOR.rule} strokeWidth={1.5} />
            <line x1={0} y1={i * cell} x2={board} y2={i * cell} stroke={COLOR.rule} strokeWidth={1.5} />
          </g>
        ))}

        {/* 3x3 邻域格子 */}
        {[-1, 0, 1].map((dy) =>
          [-1, 0, 1].map((dx) => {
            const lit = cellLit(dx + 1, dy + 1);
            const cx = qcx + dx;
            const cy = qcy + dy;
            return (
              <rect
                key={`${dx},${dy}`}
                x={cx * cell}
                y={cy * cell}
                width={cell}
                height={cell}
                fill={COLOR.accentSoft}
                opacity={lit}
              />
            );
          }),
        )}

        {/* 核半径支撑域 */}
        <circle
          cx={qx * cell}
          cy={qy * cell}
          r={cell * 1.0 * highlightR}
          fill="none"
          stroke={COLOR.accent}
          strokeWidth={3}
          strokeDasharray="10 8"
        />

        {/* 粒子 */}
        {particles.map(([px, py], i) => {
          const isSelf = px === qx && py === qy;
          return (
            <circle
              key={i}
              cx={px * cell}
              cy={py * cell}
              r={isSelf ? 11 : 8}
              fill={isSelf ? COLOR.orange : COLOR.inkSoft}
              opacity={0.9}
            />
          );
        })}

        {/* 网格容器边框 */}
        <rect x={0} y={0} width={board} height={board} fill="none" stroke={COLOR.inkSoft} strokeWidth={2.5} />
      </svg>

      <div style={{ display: "flex", flexDirection: "column", gap: 26, maxWidth: 760 }}>
        <div style={{ fontSize: 30, fontWeight: 700, color: COLOR.accent }}>
          只查 3×3×3 个格子，而不是全部粒子
        </div>

        <div style={{ display: "flex", flexDirection: "column", gap: 16, fontSize: 22, lineHeight: 1.7 }}>
          <Step
            n="1"
            title="Hash：三维格坐标 → 一维下标"
            detail="cell.x + cell.y·N + cell.z·N²，并做取模回卷处理越界"
            frame={frame}
            at={120}
          />
          <Step
            n="2"
            title="计数 → 前缀和 → 散射"
            detail="先统计每格粒子数，再前缀和求起点，最后按格写回索引"
            frame={frame}
            at={150}
          />
          <Step
            n="3"
            title="查询：begin = cellStart − cellCount"
            detail="散射后 cellStart[h] 指向该格末尾，因此起点要减掉计数"
            frame={frame}
            at={180}
          />
        </div>

        <div
          style={{
            fontFamily: FONT.mono,
            fontSize: 20,
            color: COLOR.inkMuted,
            backgroundColor: "#FFFFFF",
            padding: "14px 22px",
            borderRadius: 6,
            border: `1px solid ${COLOR.rule}`,
          }}
        >
          cellSize = 2h ⇒ 3×3×3 邻域足以覆盖半径 h 的支撑域
        </div>
      </div>
    </AbsoluteFill>
  );
};

const Step: React.FC<{
  n: string;
  title: string;
  detail: string;
  frame: number;
  at: number;
}> = ({ n, title, detail, frame, at }) => {
  const appear = interpolate(frame, [at, at + 22], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });
  const glow = pulse(frame, at + 12, 20);

  return (
    <div
      style={{
        opacity: appear,
        display: "flex",
        gap: 16,
        alignItems: "flex-start",
        transform: `translateX(${(1 - appear) * 18}px)`,
      }}
    >
      <span
        style={{
          flexShrink: 0,
          width: 34,
          height: 34,
          borderRadius: "50%",
          backgroundColor: glow > 0.3 ? COLOR.accent : COLOR.accentSoft,
          color: glow > 0.3 ? "#FFFFFF" : COLOR.accent,
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
          fontWeight: 700,
          fontSize: 19,
          marginTop: 3,
        }}
      >
        {n}
      </span>
      <div>
        <div style={{ fontWeight: 700, color: COLOR.ink }}>{title}</div>
        <div style={{ fontSize: 20, color: COLOR.inkSoft }}>{detail}</div>
      </div>
    </div>
  );
};
