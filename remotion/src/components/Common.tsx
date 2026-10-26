import { interpolate } from "remotion";
import { COLOR, FONT } from "../theme";

/**
 * 出现用的通用动画包裹：淡入 + 上浮。
 * 所有场景统一走这里，速度一致，切镜时才不会一快一慢。
 */
export const Rise: React.FC<{
  frame: number;
  delay?: number;
  duration?: number;
  distance?: number;
  children: React.ReactNode;
  style?: React.CSSProperties;
}> = ({ frame, delay = 0, duration = 16, distance = 16, children, style }) => {
  const t = interpolate(frame - delay, [0, duration], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
    easing: (x: number) => 1 - Math.pow(1 - x, 3),
  });
  return (
    <div
      style={{
        opacity: t,
        transform: `translateY(${(1 - t) * distance}px)`,
        ...style,
      }}
    >
      {children}
    </div>
  );
};

/** 带标题的卡片，用于并列要点 */
export const Card: React.FC<{
  title?: string;
  color?: string;
  soft?: string;
  children: React.ReactNode;
  style?: React.CSSProperties;
}> = ({ title, color = COLOR.accent, soft = COLOR.accentSoft, children, style }) => (
  <div
    style={{
      flex: 1,
      padding: "22px 26px",
      backgroundColor: soft,
      borderLeft: `5px solid ${color}`,
      borderRadius: 6,
      fontFamily: FONT.sans,
      ...style,
    }}
  >
    {title ? (
      <div
        style={{
          fontSize: 25,
          fontWeight: 700,
          color,
          marginBottom: 10,
          letterSpacing: 0.5,
        }}
      >
        {title}
      </div>
    ) : null}
    <div style={{ fontSize: 22, lineHeight: 1.75, color: COLOR.ink }}>
      {children}
    </div>
  </div>
);

/**
 * 确定性伪随机。
 *
 * 绝不能直接用 Math.random()：Remotion 逐帧独立渲染，
 * 随机数会导致同一帧每次算出不同结果，成片里表现为闪烁噪点。
 */
export const seeded = (seed: number) => {
  let state = seed >>> 0;
  return () => {
    state = (state * 1664525 + 1013904223) >>> 0;
    return state / 4294967296;
  };
};

/** 以某一帧为中心的高斯型时间窗，用于“此刻高亮”效果 */
export const pulse = (
  frame: number,
  center: number,
  spread = 14,
): number => {
  const d = (frame - center) / spread;
  return Math.exp(-d * d);
};
