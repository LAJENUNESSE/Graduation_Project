import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { COLOR, FONT } from "../theme";
import { Card } from "../components/Common";

/** 09 稳定化措施：让公式在真实场景里跑稳 */
export const Stabilization: React.FC = () => {
  const frame = useCurrentFrame();

  const items = [
    {
      title: "kernel 系数 CPU 预计算",
      body: "poly6Coeff / spikyCoeff 在 CPU 侧算好再上传，GPU 每粒子每邻居只做一次乘法，省掉重复的高次幂运算。",
      color: COLOR.accent,
      soft: COLOR.accentSoft,
    },
    {
      title: "density 下限保护",
      body: "densityI < 0.0001 时钳到 0.0001，防止粘性与表面张力项里出现接近 0 的分母。",
      color: COLOR.orange,
      soft: COLOR.orangeSoft,
    },
    {
      title: "新生粒子 warm-up",
      body: "SPH_WARMUP_TIME = 0.08s，仅对压力项做 α = clamp(age/warmup, 0, 1) 渐入，解决 burst 发射瞬间粒子被高密度环境弹飞的闪烁。",
      color: COLOR.green,
      soft: COLOR.greenSoft,
    },
    {
      title: "加速度限幅",
      body: "|a| > 500 时按比例缩放。不是最物理正确，但能在极端参数下保住整个系统不炸。",
      color: COLOR.purple,
      soft: COLOR.purpleSoft,
    },
    {
      title: "PCISPH 路径重力去重",
      body: "PCISPH 已在预测/修正里考虑过重力，simulate pass 再叠加会双算，所以该路径下 simGravity 置零。",
      color: COLOR.red,
      soft: COLOR.redSoft,
    },
    {
      title: "刚体数据统一布局",
      body: "GPURigidBodyData 同时服务 GL shader 与 CUDA pipeline，保证两种后端的边界耦合语义一致。",
      color: COLOR.accent,
      soft: COLOR.accentSoft,
    },
  ];

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bg,
        fontFamily: FONT.sans,
        flexDirection: "column",
        justifyContent: "center",
        gap: 20,
      }}
    >
      <div
        style={{
          textAlign: "center",
          fontSize: 30,
          fontWeight: 700,
          color: COLOR.ink,
          marginBottom: 6,
          opacity: interpolate(frame, [0, 20], [0, 1], { extrapolateRight: "clamp" }),
        }}
      >
        真正的难点不是写出公式，而是把它跑稳
      </div>

      <div style={{ display: "flex", gap: 20 }}>
        {items.slice(0, 3).map((it, i) => (
          <AnimatedCard key={it.title} item={it} frame={frame} at={20 + i * 26} />
        ))}
      </div>
      <div style={{ display: "flex", gap: 20, marginTop: 4 }}>
        {items.slice(3).map((it, i) => (
          <AnimatedCard key={it.title} item={it} frame={frame} at={110 + i * 26} />
        ))}
      </div>
    </AbsoluteFill>
  );
};

const AnimatedCard: React.FC<{
  item: { title: string; body: string; color: string; soft: string };
  frame: number;
  at: number;
}> = ({ item, frame, at }) => {
  const t = interpolate(frame, [at, at + 24], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
    easing: (x) => 1 - Math.pow(1 - x, 3),
  });
  return (
    <div
      style={{
        flex: 1,
        opacity: t,
        transform: `translateY(${(1 - t) * 20}px)`,
      }}
    >
      <Card title={item.title} color={item.color} soft={item.soft} style={{ height: "100%" }}>
        <span style={{ fontSize: 20, lineHeight: 1.65 }}>{item.body}</span>
      </Card>
    </div>
  );
};

/** 10 双后端一致性：同一套数学，两种执行后端 */
export const Backends: React.FC = () => {
  const frame = useCurrentFrame();
  const inA = interpolate(frame, [0, 24], [0, 1], { extrapolateRight: "clamp" });
  const inB = interpolate(frame, [40, 70], [0, 1], { extrapolateRight: "clamp" });
  const inC = interpolate(frame, [110, 145], [0, 1], { extrapolateRight: "clamp" });

  const steps = [
    "LaunchSPHGridBuild",
    "LaunchSPHDensity",
    "LaunchSPHForce",
    "LaunchPCISPHInit",
    "LaunchPCISPHPredict",
    "LaunchPCISPHDensity",
    "LaunchPCISPHForce",
    "LaunchPCISPHApply",
    "LaunchSPHSimulate",
  ];

  return (
    <AbsoluteFill
      style={{
        backgroundColor: COLOR.bgAlt,
        fontFamily: FONT.sans,
        flexDirection: "column",
        justifyContent: "center",
        alignItems: "center",
        gap: 34,
      }}
    >
      <div style={{ opacity: inA, fontSize: 32, fontWeight: 700, color: COLOR.ink, textAlign: "center" }}>
        同一套数值管线，两种执行后端
      </div>

      <div style={{ display: "flex", gap: 90, alignItems: "center", opacity: inB }}>
        {/* GL 侧 */}
        <div style={{ display: "flex", flexDirection: "column", gap: 12, alignItems: "center" }}>
          <div style={{ fontSize: 25, fontWeight: 700, color: COLOR.accent }}>
            OpenGL Compute
          </div>
          <div style={{ fontFamily: FONT.mono, fontSize: 19, color: COLOR.inkSoft, lineHeight: 1.9, textAlign: "center" }}>
            sph_density.glsl
            <br />
            sph_force.glsl
            <br />
            sph_pcisph_*.glsl
          </div>
        </div>

        <div style={{ fontSize: 40, color: COLOR.inkMuted }}>≡</div>

        {/* CUDA 侧 */}
        <div style={{ display: "flex", flexDirection: "column", gap: 12, alignItems: "center" }}>
          <div style={{ fontSize: 25, fontWeight: 700, color: COLOR.green }}>
            CUDA
          </div>
          <div style={{ fontFamily: FONT.mono, fontSize: 19, color: COLOR.inkSoft, lineHeight: 1.9, textAlign: "center" }}>
            CudaSPHPipeline.cu
            <br />
            kernel 一一对应
            <br />
            CUB DeviceScan 替代 GL 前缀和
          </div>
        </div>
      </div>

      {/* 一组一一对应的入口 */}
      <div
        style={{
          opacity: inB,
          display: "flex",
          flexWrap: "wrap",
          gap: 10,
          justifyContent: "center",
          maxWidth: 1300,
        }}
      >
        {steps.map((s, i) => {
          const t = interpolate(frame, [70 + i * 6, 86 + i * 6], [0, 1], {
            extrapolateLeft: "clamp",
            extrapolateRight: "clamp",
          });
          return (
            <span
              key={s}
              style={{
                opacity: t,
                fontFamily: FONT.mono,
                fontSize: 18,
                padding: "7px 14px",
                backgroundColor: "#FFFFFF",
                border: `1.5px solid ${COLOR.rule}`,
                borderRadius: 4,
                color: COLOR.inkSoft,
              }}
            >
              {s}
            </span>
          );
        })}
      </div>

      <div style={{ opacity: inC, display: "flex", gap: 24, width: 1560 }}>
        <Card title="CUDA 不是另一套物理" color={COLOR.green} soft={COLOR.greenSoft}>
          差别只在执行后端、scan/kernel 并行实现与性能特性，数学语义完全一致。
          这让「加速比」这个数字才有比较意义。
        </Card>
        <Card title="故障自动回退" color={COLOR.orange} soft={COLOR.orangeSoft}>
          CUDA 侧有错误检测与全局中毒标志：一旦出错永久回退到 OpenGL Compute 路径，
          画面不会因为驱动问题直接崩掉。
        </Card>
      </div>
    </AbsoluteFill>
  );
};
