import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { COLOR, FONT } from "../theme";
import { Card } from "../components/Common";

/** 09 参数不是平行滑条：三层结构 */
export const ParamLayers: React.FC = () => {
  const frame = useCurrentFrame();

  const layers = [
    {
      name: "尺度参数",
      keys: "h · m · ρ₀ · Δt",
      color: COLOR.accent,
      soft: COLOR.accentSoft,
      body: "定系统主尺度。改它们等于改变「合理的粒子间距」——spacing ≈ (m/ρ₀)^(1/3)，同时牵动 poly6Coeff、spikyCoeff、cellSize=2h 与 PCISPH 的 δ。",
    },
    {
      name: "刚度参数",
      keys: "GasConstant · δ · Iterations",
      color: COLOR.orange,
      soft: COLOR.orangeSoft,
      body: "定流体「有多硬」。k 越大体积保持越强但越容易爆；δ 决定每轮补多少压力，Iterations 决定补几次。两者不要同时激进。",
    },
    {
      name: "风格参数",
      keys: "Viscosity · SurfaceTension · Boundary*",
      color: COLOR.purple,
      soft: COLOR.purpleSoft,
      body: "定观感。粘性抑制噪声但过高会果冻感；表面张力让小液滴更圆润；边界刚度/阻尼决定贴墙表现。",
    },
  ];

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
      <div
        style={{
          textAlign: "center",
          fontSize: 30,
          fontWeight: 700,
          color: COLOR.ink,
          opacity: interpolate(frame, [0, 22], [0, 1], { extrapolateRight: "clamp" }),
        }}
      >
        十几个参数不是平行滑条，按三层看才不会乱
      </div>

      {layers.map((layer, i) => {
        const at = 30 + i * 55;
        const t = interpolate(frame, [at, at + 26], [0, 1], {
          extrapolateLeft: "clamp",
          extrapolateRight: "clamp",
          easing: (x) => 1 - Math.pow(1 - x, 3),
        });
        return (
          <div
            key={layer.name}
            style={{
              opacity: t,
              transform: `translateX(${(1 - t) * -28}px)`,
              display: "flex",
              alignItems: "center",
              gap: 30,
              padding: "20px 28px",
              backgroundColor: layer.soft,
              borderLeft: `6px solid ${layer.color}`,
              borderRadius: 6,
            }}
          >
            <div style={{ width: 190, flexShrink: 0 }}>
              <div style={{ fontSize: 26, fontWeight: 700, color: layer.color }}>
                {layer.name}
              </div>
              <div
                style={{
                  fontFamily: FONT.mono,
                  fontSize: 18,
                  color: COLOR.inkSoft,
                  marginTop: 4,
                }}
              >
                {layer.keys}
              </div>
            </div>
            <div style={{ fontSize: 21, lineHeight: 1.7, color: COLOR.ink }}>
              {layer.body}
            </div>
          </div>
        );
      })}

      <div
        style={{
          opacity: interpolate(frame, [210, 240], [0, 1], { extrapolateRight: "clamp" }),
          display: "flex",
          gap: 24,
          marginTop: 6,
        }}
      >
        <Card title="推荐调参顺序" color={COLOR.green} soft={COLOR.greenSoft}>
          <span style={{ fontSize: 20 }}>
            先定 Δt → 再定 h → 协调 m 与 ρ₀ → 调 GasConstant → 补 Viscosity →
            最后才动 Iterations / δ / 表面张力 / 边界。
          </span>
        </Card>
        <Card title="编辑器的滑条范围很宽" color={COLOR.orange} soft={COLOR.orangeSoft}>
          <span style={{ fontSize: 20 }}>
            GasConstant 可调到 50000、h 可到 2.0——空间大也意味着很容易调出稳定区间。
          </span>
        </Card>
      </div>
    </AbsoluteFill>
  );
};

/** 12 GPU 数据布局：为什么计算能这么规整 */
export const DataLayout: React.FC = () => {
  const frame = useCurrentFrame();

  const fields = [
    { name: "posAndLife", detail: "xyz = 位置，w = 剩余寿命", color: COLOR.accent },
    { name: "velAndMaxLife", detail: "xyz = 速度，w = 最大寿命", color: COLOR.green },
    { name: "startColor / endColor", detail: "粒子颜色（渲染用）", color: COLOR.inkMuted },
    { name: "params", detail: "xy = 尺寸，z = 密度，w = 压力", color: COLOR.orange },
  ];

  const inA = interpolate(frame, [0, 24], [0, 1], { extrapolateRight: "clamp" });
  const inB = interpolate(frame, [70, 100], [0, 1], { extrapolateRight: "clamp" });

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
      <div style={{ display: "flex", flexDirection: "column", gap: 14, opacity: inA }}>
        <div style={{ fontSize: 26, fontWeight: 700, color: COLOR.ink, marginBottom: 6 }}>
          单个粒子 = 5 个 vec4 = 80 字节
        </div>
        {fields.map((f, i) => {
          const t = interpolate(frame, [18 + i * 14, 40 + i * 14], [0, 1], {
            extrapolateLeft: "clamp",
            extrapolateRight: "clamp",
          });
          return (
            <div
              key={f.name}
              style={{
                opacity: t,
                display: "flex",
                alignItems: "center",
                gap: 16,
              }}
            >
              <div
                style={{
                  width: 232,
                  padding: "12px 16px",
                  backgroundColor: "#FFFFFF",
                  border: `2px solid ${f.color}`,
                  borderRadius: 4,
                  fontFamily: FONT.mono,
                  fontSize: 19,
                  color: f.color,
                }}
              >
                {f.name}
              </div>
              <span style={{ fontSize: 20, color: COLOR.inkSoft }}>{f.detail}</span>
            </div>
          );
        })}
      </div>

      <div style={{ display: "flex", flexDirection: "column", gap: 20, maxWidth: 700, opacity: inB }}>
        <div style={{ fontSize: 26, fontWeight: 700, color: COLOR.accent }}>
          三个值得注意的布局选择
        </div>
        <ListItem
          title="密度与压力塞进 params.zw"
          body="复用已有的颜色/尺寸字段，不额外开一个每粒子的密度缓冲，省一次显存占用与一次带宽往返。"
          frame={frame}
          at={110}
        />
        <ListItem
          title="alive 索引与 sorted 索引分离"
          body="aliveIndices 提供紧凑的存活列表，sortedIndices 提供按格排序的邻域遍历序，两者职责不同不能合并。"
          frame={frame}
          at={150}
        />
        <ListItem
          title="SPH 参数走 UBO / 常量内存"
          body="所有粒子只读同一份参数，放常量内存命中率更高；CUDA 侧对应 __constant__ + cudaMemcpyToSymbol。"
          frame={frame}
          at={190}
        />
      </div>
    </AbsoluteFill>
  );
};

/** 13 两种求解器的取舍：什么时候用哪个 */
export const SolverChoice: React.FC = () => {
  const frame = useCurrentFrame();

  const rows = [
    { key: "不可压缩性", wc: "近似（一次性压力）", pc: "迭代逼近 ρ₀", },
    { key: "单帧成本", wc: "低（Density + Force 两趟）", pc: "高（含 I 次 Predict/Density*/Force）" },
    { key: "典型耗时 50k", wc: "3.9 ~ 6.0 ms", pc: "76 ~ 91 ms" },
    { key: "适用场景", wc: "大规模视觉粒子、轻负载", pc: "体积守恒要求高、刚体耦合强" },
    { key: "仓库默认", wc: "可选", pc: "默认开启，迭代 3 次" },
  ];

  const inA = interpolate(frame, [0, 24], [0, 1], { extrapolateRight: "clamp" });

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bg,
        fontFamily: FONT.sans,
        flexDirection: "column",
        justifyContent: "center",
        alignItems: "center",
        gap: 34,
      }}
    >
      <div style={{ opacity: inA, fontSize: 30, fontWeight: 700, color: COLOR.ink }}>
        同一套 SPH 骨架，两种压力求解策略
      </div>

      <div style={{ opacity: inA, width: 1500 }}>
        {/* 表头 */}
        <div style={{ display: "flex", borderBottom: `3px solid ${COLOR.ink}`, paddingBottom: 10 }}>
          <div style={{ width: 260, fontSize: 22, fontWeight: 700, color: COLOR.inkMuted }}>对比项</div>
          <div style={{ flex: 1, fontSize: 24, fontWeight: 700, color: COLOR.accent, textAlign: "center" }}>
            WCSPH
          </div>
          <div style={{ flex: 1, fontSize: 24, fontWeight: 700, color: COLOR.orange, textAlign: "center" }}>
            PCISPH
          </div>
        </div>

        {rows.map((r, i) => {
          const at = 40 + i * 40;
          const t = interpolate(frame, [at, at + 24], [0, 1], {
            extrapolateLeft: "clamp",
            extrapolateRight: "clamp",
          });
          return (
            <div
              key={r.key}
              style={{
                display: "flex",
                alignItems: "center",
                opacity: t,
                borderBottom: `1px solid ${COLOR.rule}`,
                padding: "15px 0",
                backgroundColor: i % 2 === 0 ? "transparent" : COLOR.bgAlt,
              }}
            >
              <div style={{ width: 260, fontSize: 21, color: COLOR.inkMuted }}>{r.key}</div>
              <div style={{ flex: 1, fontSize: 21, textAlign: "center", color: COLOR.ink }}>{r.wc}</div>
              <div style={{ flex: 1, fontSize: 21, textAlign: "center", color: COLOR.ink }}>{r.pc}</div>
            </div>
          );
        })}
      </div>

      <div
        style={{
          opacity: interpolate(frame, [260, 300], [0, 1], { extrapolateRight: "clamp" }),
          maxWidth: 1380,
          textAlign: "center",
          fontSize: 23,
          lineHeight: 1.8,
          color: COLOR.inkSoft,
        }}
      >
        注意 50k 档的差距不是实现优劣，而是算法复杂度差异：
        PCISPH 每帧多跑 I 次密度与压力重算，换来的是体积守恒。
        这也解释了为什么 WCSPH 加速比能到 1.55× 而 PCISPH 稳定在 1.2 ~ 1.4×。
      </div>
    </AbsoluteFill>
  );
};

/** 14 常见失败模式与排查 */
export const FailureModes: React.FC = () => {
  const frame = useCurrentFrame();

  const modes = [
    {
      symptom: "粒子爆炸",
      causes: "δ 太大 · Δt 太大 · density 接近 0 触发大除法 · grid 与预测 grid 不一致",
      color: COLOR.red,
      soft: COLOR.redSoft,
    },
    {
      symptom: "粒子粘团",
      causes: "负压力或压力过弱 · viscosity 过高 · rest density 与初始间距不匹配",
      color: COLOR.orange,
      soft: COLOR.orangeSoft,
    },
    {
      symptom: "刚体边界抖动",
      causes: "boundary stiffness 太大 · damping 太低 · 刚体速度与粒子预测速度不一致",
      color: COLOR.purple,
      soft: COLOR.purpleSoft,
    },
    {
      symptom: "画面闪烁",
      causes: "新生粒子直接承受全压力 · 多 pass 缺 barrier · 结构布局或 binding 错位",
      color: COLOR.accent,
      soft: COLOR.accentSoft,
    },
  ];

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bgAlt,
        fontFamily: FONT.sans,
        flexDirection: "column",
        justifyContent: "center",
        gap: 22,
      }}
    >
      <div
        style={{
          textAlign: "center",
          fontSize: 30,
          fontWeight: 700,
          color: COLOR.ink,
          opacity: interpolate(frame, [0, 22], [0, 1], { extrapolateRight: "clamp" }),
          marginBottom: 8,
        }}
      >
        调试时先认现象，再反查参数
      </div>

      {modes.map((m, i) => {
        const at = 30 + i * 45;
        const t = interpolate(frame, [at, at + 26], [0, 1], {
          extrapolateLeft: "clamp",
          extrapolateRight: "clamp",
          easing: (x) => 1 - Math.pow(1 - x, 3),
        });
        return (
          <div
            key={m.symptom}
            style={{
              opacity: t,
              transform: `translateY(${(1 - t) * 16}px)`,
              display: "flex",
              alignItems: "center",
              gap: 28,
              padding: "18px 26px",
              backgroundColor: m.soft,
              borderLeft: `6px solid ${m.color}`,
              borderRadius: 6,
              width: 1560,
              alignSelf: "center",
            }}
          >
            <div style={{ width: 210, flexShrink: 0, fontSize: 25, fontWeight: 700, color: m.color }}>
              {m.symptom}
            </div>
            <div style={{ fontSize: 21, lineHeight: 1.65, color: COLOR.ink }}>{m.causes}</div>
          </div>
        );
      })}

      <div
        style={{
          opacity: interpolate(frame, [230, 265], [0, 1], { extrapolateRight: "clamp" }),
          textAlign: "center",
          fontSize: 22,
          color: COLOR.inkSoft,
          marginTop: 10,
        }}
      >
        这些不是「玄学调参」——每一个都对应上一页某条稳定化措施失效后的具体表现。
      </div>
    </AbsoluteFill>
  );
};

const ListItem: React.FC<{
  title: string;
  body: string;
  frame: number;
  at: number;
}> = ({ title, body, frame, at }) => {
  const t = interpolate(frame, [at, at + 24], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });
  return (
    <div style={{ opacity: t, transform: `translateX(${(1 - t) * 20}px)` }}>
      <div style={{ fontSize: 22, fontWeight: 700, color: COLOR.ink }}>{title}</div>
      <div style={{ fontSize: 20, color: COLOR.inkSoft, lineHeight: 1.7 }}>{body}</div>
    </div>
  );
};
