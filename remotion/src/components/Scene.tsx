import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { COLOR, FONT, PAD } from "../theme";

/**
 * 单个镜头的统一版式：左上角章节号 + 标题，底部字幕条，中间留给示意图/公式。
 *
 * 所有镜头共用同一套骨架，切换时标题位置不跳动，看起来才像一支完整的片子。
 */
export const Scene: React.FC<{
  /** 章节号，如 "01" */
  index?: string;
  /** 镜头标题 */
  title?: string;
  /** 右上角的补充说明，一般是源码锚点 */
  anchor?: string;
  /** 底部字幕，按时间分段 */
  captions?: Caption[];
  /** 背景色，默认根据奇偶镜头交替 */
  tone?: "plain" | "tint";
  children: React.ReactNode;
}> = ({ index, title, anchor, captions = [], tone = "plain", children }) => {
  const frame = useCurrentFrame();
  const fadeIn = interpolate(frame, [0, 12], [0, 1], {
    extrapolateRight: "clamp",
  });

  return (
    <AbsoluteFill
      style={{
        backgroundColor: tone === "tint" ? COLOR.bgAlt : COLOR.bg,
        fontFamily: FONT.sans,
        color: COLOR.ink,
      }}
    >
      {/* 顶部标题区 */}
      <div
        style={{
          position: "absolute",
          top: PAD * 0.55,
          left: PAD,
          right: PAD,
          display: "flex",
          alignItems: "baseline",
          gap: 20,
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
          <span style={{ fontSize: 40, fontWeight: 600, letterSpacing: 1 }}>
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

      {index || title ? (
        <div
          style={{
            position: "absolute",
            top: PAD * 0.55 + 66,
            left: PAD,
            right: PAD,
            height: 2,
            backgroundColor: COLOR.rule,
            opacity: fadeIn,
          }}
        />
      ) : null}

      {/* 主内容区 */}
      <div
        style={{
          position: "absolute",
          top: PAD * 0.55 + 100,
          left: PAD,
          right: PAD,
          bottom: captions.length > 0 ? 190 : PAD * 0.55,
          display: "flex",
          flexDirection: "column",
          justifyContent: "center",
        }}
      >
        {children}
      </div>

      {captions.length > 0 ? <CaptionBar captions={captions} /> : null}
    </AbsoluteFill>
  );
};

export type Caption = {
  /** 起始帧，相对本镜头 */
  from: number;
  text: string;
};

const CaptionBar: React.FC<{ captions: Caption[] }> = ({ captions }) => {
  const frame = useCurrentFrame();
  const current = [...captions].reverse().find((c) => frame >= c.from);
  if (!current) {
    return null;
  }
  // 每条字幕进入时淡入，避免硬切
  const local = frame - current.from;
  const opacity = interpolate(local, [0, 8], [0, 1], {
    extrapolateRight: "clamp",
  });

  return (
    <div
      style={{
        position: "absolute",
        left: PAD,
        right: PAD,
        bottom: 56,
        minHeight: 88,
        display: "flex",
        alignItems: "center",
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
          backgroundColor: "rgba(255, 255, 255, 0.82)",
          borderLeft: `4px solid ${COLOR.accent}`,
          borderRadius: 4,
        }}
      >
        {current.text}
      </div>
    </div>
  );
};
