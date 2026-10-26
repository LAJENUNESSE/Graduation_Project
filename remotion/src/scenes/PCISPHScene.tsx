import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { COLOR, FONT } from "../theme";
import { Math as MathBlock, Sub, Sup, V, Vec, Frac } from "../components/Math";
import { Card } from "../components/Common";

/** 07 PCISPH：预测 → 校正迭代 */
export const PCISPH: React.FC = () => {
  const frame = useCurrentFrame();

  // 迭代过程：密度误差逐轮收敛
  const iterations = 6;
  const start = 60;
  const perIter = 34;
  const rawError = 240; // 初始压缩导致的密度超调

  const currentIter = Math.min(
    iterations,
    Math.max(0, Math.floor((frame - start) / perIter) + 1),
  );
  // 简化的收敛曲线：每轮按 (1 - δ·k_c) 衰减，纯演示用
  const residual = (iter: number) => rawError * Math.pow(0.42, iter);
  const activeIter = Math.min(iterations, Math.max(1, currentIter));
  const shownError =
    frame < start ? rawError : residual(activeIter);

  const passes = [
    { key: "Density", label: "密度", color: COLOR.accent },
    { key: "PCISPHInit", label: "Init", color: COLOR.green },
    { key: "Predict", label: "Predict", color: COLOR.orange },
    { key: "Density*", label: "Density*", color: COLOR.orange },
    { key: "Force", label: "Force", color: COLOR.orange },
    { key: "Apply", label: "Apply", color: COLOR.purple },
    { key: "Simulate", label: "Simulate", color: COLOR.inkSoft },
  ];

  // 循环体（Predict/Density*/Force）在迭代期间反复高亮
  const loopPhase = frame < start ? 0 : (frame - start) % perIter;
  const loopIdx = Math.floor(loopPhase / (perIter / 3));

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
      {/* pass 链路 */}
      <div style={{ display: "flex", alignItems: "center", justifyContent: "center", gap: 8 }}>
        {passes.map((p, i) => {
          const isLoop = i >= 2 && i <= 4;
          const active = isLoop && loopIdx === i - 2 && frame >= start;
          const post = i >= 5 && frame >= start + iterations * perIter;
          return (
            <div key={p.key} style={{ display: "flex", alignItems: "center", gap: 8 }}>
              <div
                style={{
                  padding: "12px 20px",
                  borderRadius: 6,
                  fontFamily: FONT.mono,
                  fontSize: 21,
                  backgroundColor: active || post ? p.color : "#FFFFFF",
                  color: active || post ? "#FFFFFF" : COLOR.inkSoft,
                  border: `2px solid ${p.color}`,
                  fontWeight: active ? 700 : 400,
                  transform: `scale(${active ? 1.06 : 1})`,
                }}
              >
                {p.label}
              </div>
              {i < passes.length - 1 ? (
                <span style={{ color: COLOR.inkMuted, fontSize: 22 }}>→</span>
              ) : null}
            </div>
          );
        })}
      </div>

      {/* 迭代循环标注 */}
      <div
        style={{
          display: "flex",
          justifyContent: "center",
          opacity: frame >= start ? 1 : 0,
          height: 30,
        }}
      >
        <div
          style={{
            fontSize: 20,
            color: COLOR.orange,
            fontFamily: FONT.mono,
          }}
        >
          └─ Predict → Density* → Force 循环 × {iterations} 次，逐轮把预测密度压回 ρ₀ ─┘
        </div>
      </div>

      <div style={{ display: "flex", gap: 46, alignItems: "flex-start", justifyContent: "center" }}>
        {/* 公式区 */}
        <div style={{ display: "flex", flexDirection: "column", gap: 14, maxWidth: 800 }}>
          <div style={{ display: "flex", justifyContent: "center" }}>
            <MathBlock size={30}>
              <Vec>v</Vec>
              <Sub>i</Sub>
              <Sup>*</Sup> = <Vec>v</Vec>
              <Sub>i</Sub> + Δ<V>t</V>(<Vec>a</Vec>
              <Sub>i</Sub>
              <Sup>non-pressure</Sup> + <Vec>a</Vec>
              <Sub>i</Sub>
              <Sup>pressure</Sup>)
            </MathBlock>
          </div>
          <div style={{ display: "flex", justifyContent: "center" }}>
            <MathBlock size={30}>
              <Vec>x</Vec>
              <Sub>i</Sub>
              <Sup>*</Sup> = <Vec>x</Vec>
              <Sub>i</Sub> + Δ<V>t</V> <Vec>v</Vec>
              <Sub>i</Sub>
              <Sup>*</Sup>
            </MathBlock>
          </div>
          <div style={{ display: "flex", justifyContent: "center" }}>
            <MathBlock size={30}>
              <V>P</V>
              <Sub>i</Sub>
              <Sup>k+1</Sup> = <V>P</V>
              <Sub>i</Sub>
              <Sup>k</Sup> + <V>δ</V> · max(0, <V>ρ</V>
              <Sub>i</Sub>
              <Sup>*</Sup> − <V>ρ</V>
              <Sub>0</Sub>)
            </MathBlock>
          </div>
          <div
            style={{
              display: "flex",
              justifyContent: "center",
              marginTop: 6,
            }}
          >
            <MathBlock size={30}>
              <Vec>a</Vec>
              <Sub>i</Sub>
              <Sup>pressure</Sup> = − Σ<Sub>j</Sub> <V>m</V>
              <Sub>j</Sub>(
              <Frac
                num={
                  <>
                    <V>P</V>
                    <Sub>i</Sub>
                  </>
                }
                den={
                  <>
                    <V>ρ</V>
                    <Sub>i</Sub>
                    <Sup>2</Sup>
                  </>
                }
              />
              +
              <Frac
                num={
                  <>
                    <V>P</V>
                    <Sub>j</Sub>
                  </>
                }
                den={
                  <>
                    <V>ρ</V>
                    <Sub>j</Sub>
                    <Sup>2</Sup>
                  </>
                }
              />
              )∇<V>W</V>
              <Sub>spiky</Sub>
            </MathBlock>
          </div>
        </div>

        {/* 收敛可视化 */}
        <div
          style={{
            display: "flex",
            flexDirection: "column",
            gap: 18,
            padding: "24px 30px",
            backgroundColor: COLOR.bgAlt,
            borderRadius: 8,
            minWidth: 560,
          }}
        >
          <div style={{ fontSize: 23, fontWeight: 700, color: COLOR.ink }}>
            预测密度误差的收敛
          </div>

          {/* 密度柱和 ρ₀ 参考线 */}
          <div style={{ position: "relative", height: 60, backgroundColor: "#FFFFFF", borderRadius: 4 }}>
            <div
              style={{
                position: "absolute",
                left: 0,
                bottom: 0,
                height: "100%",
                width: `${Math.min(100, ((1000 + shownError) / 1400) * 100)}%`,
                backgroundColor: shownError > 60 ? COLOR.red : COLOR.green,
                borderRadius: 4,
                opacity: 0.85,
              }}
            />
            {/* ρ₀ 位置 */}
            <div
              style={{
                position: "absolute",
                left: `${(1000 / 1400) * 100}%`,
                top: -8,
                bottom: -8,
                width: 3,
                backgroundColor: COLOR.ink,
              }}
            />
            <span
              style={{
                position: "absolute",
                left: `calc(${(1000 / 1400) * 100}% + 8px)`,
                top: -30,
                fontSize: 18,
                color: COLOR.ink,
                fontFamily: FONT.serif,
              }}
            >
              ρ₀ = 1000
            </span>
          </div>

          <div style={{ display: "flex", justifyContent: "space-between", fontSize: 20 }}>
            <span style={{ color: COLOR.inkMuted }}>迭代</span>
            <span style={{ fontFamily: FONT.mono, color: COLOR.ink, fontSize: 23 }}>
              {frame < start ? 0 : Math.min(iterations, activeIter)} / {iterations}
            </span>
            <span style={{ color: COLOR.inkMuted }}>残余误差</span>
            <span
              style={{
                fontFamily: FONT.mono,
                color: shownError < 60 ? COLOR.green : COLOR.red,
                fontSize: 23,
              }}
            >
              {shownError.toFixed(0)}
            </span>
          </div>

          <div
            style={{
              fontSize: 19,
              color: COLOR.inkSoft,
              lineHeight: 1.65,
              borderTop: `1px solid ${COLOR.rule}`,
              paddingTop: 14,
            }}
          >
            δ 由 <span style={{ fontFamily: FONT.mono }}>ComputePCISPHDelta()</span> 依
            Solenthaler 2009 推导：在立方格点上枚举 [-h, h] 邻居累加 Spiky 梯度，
            得到 β ∝ Δt²m²，δ ≈ −1/β。δ 太小修正不足，太大会压力过冲振荡。
          </div>
        </div>
      </div>

      <div
        style={{
          display: "flex",
          gap: 24,
          opacity: interpolate(frame, [250, 280], [0, 1], { extrapolateRight: "clamp" }),
        }}
      >
        <Card title="为何不一轮算完压力" color={COLOR.accent} soft={COLOR.accentSoft}>
          WCSPH 的一次性压力只近似不可压缩；时间步偏大或刚体耦合强时密度波动明显。
          PCISPH 把不可压缩性从「直接求值」改成「迭代逼近」。
        </Card>
        <Card title="成本模型" color={COLOR.orange} soft={COLOR.orangeSoft}>
          T ≈ I·(T_predict + T_density* + T_force)。
          迭代次数 I 是精度与耗时的直接交换比，仓库默认 3 次，可调 1~8。
        </Card>
      </div>
    </AbsoluteFill>
  );
};

/** 08 折中策略：预测位置重建 grid */
export const GridRebuild: React.FC = () => {
  const frame = useCurrentFrame();
  const inA = interpolate(frame, [0, 24], [0, 1], { extrapolateRight: "clamp" });
  const inB = interpolate(frame, [60, 90], [0, 1], { extrapolateRight: "clamp" });

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bgAlt,
        fontFamily: FONT.sans,
        flexDirection: "column",
        justifyContent: "center",
        alignItems: "center",
        gap: 50,
      }}
    >
      <div style={{ opacity: inA, fontSize: 34, fontWeight: 700, color: COLOR.ink }}>
        每轮都重建预测 grid 太贵，始终不重建又会失真
      </div>

      <div style={{ display: "flex", gap: 40, width: 1640 }}>
        <Card title="方案 A：每轮都重建" color={COLOR.orange} soft={COLOR.orangeSoft}>
          <div style={{ marginBottom: 8 }}>邻域关系始终与预测位置一致，修正最准确。</div>
          <div style={{ color: COLOR.red }}>代价：每轮多一次完整的 grid build。</div>
        </Card>
        <Card title="方案 B：始终不重建" color={COLOR.accent} soft={COLOR.accentSoft}>
          <div style={{ marginBottom: 8 }}>省掉重复的构建开销，最便宜。</div>
          <div style={{ color: COLOR.red }}>
            代价：预测位置越偏，邻域关系越假，PCISPH 修正效果打折。
          </div>
        </Card>
        <Card title="仓库采用：折中" color={COLOR.green} soft={COLOR.greenSoft}>
          <div style={{ marginBottom: 8 }}>
            迭代 0 沿用已有 grid 起步；迭代 1 起按预测位置重建。
          </div>
          <div style={{ color: COLOR.green }}>
            明显改善收敛，同时避免一开始就多做一次昂贵构建。
          </div>
        </Card>
      </div>

      {/* 时间轴示意 */}
      <div style={{ display: "flex", alignItems: "center", gap: 0, opacity: inB }}>
        {[
          { label: "迭代 0", sub: "用现有 grid", color: COLOR.accent },
          { label: "迭代 1", sub: "按预测位置重建", color: COLOR.green },
          { label: "迭代 2", sub: "重建", color: COLOR.green },
          { label: "…", sub: "重建", color: COLOR.green },
        ].map((s, i) => {
          const active = frame >= 90 + i * 12;
          return (
            <div key={i} style={{ display: "flex", alignItems: "center" }}>
              <div
                style={{
                  padding: "14px 26px",
                  borderRadius: 6,
                  backgroundColor: active ? s.color : "#FFFFFF",
                  color: active ? "#FFFFFF" : COLOR.inkMuted,
                  border: `2px solid ${s.color}`,
                  textAlign: "center",
                }}
              >
                <div style={{ fontWeight: 700, fontSize: 21 }}>{s.label}</div>
                <div style={{ fontSize: 17, opacity: 0.9 }}>{s.sub}</div>
              </div>
              {i < 3 ? <span style={{ margin: "0 10px", color: COLOR.inkMuted }}>→</span> : null}
            </div>
          );
        })}
      </div>

      <div
        style={{
          opacity: interpolate(frame, [150, 180], [0, 1], { extrapolateRight: "clamp" }),
          fontFamily: FONT.mono,
          fontSize: 20,
          color: COLOR.inkMuted,
        }}
      >
        ParticleSystemGPU.cpp：u_UsePredictedPos 控制 density pass 读原位置还是预测位置
      </div>
    </AbsoluteFill>
  );
};
