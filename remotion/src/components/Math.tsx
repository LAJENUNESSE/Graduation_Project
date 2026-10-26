import { interpolate } from "remotion";
import { COLOR, FONT } from "../theme";

/**
 * 手写的轻量数学排版组件。
 *
 * 刻意不引入 KaTeX：它会带上外部字体文件，headless 渲染时一旦字体没加载好
 * 就会静默降级成难看的回退字形。这里全部用系统字体 + CSS 排版，
 * 渲染结果完全确定，也不依赖网络。
 */

type MathProps = {
  children: React.ReactNode;
  /** 公式整体字号，默认 40 */
  size?: number;
  /** 是否独占一行居中 */
  block?: boolean;
  style?: React.CSSProperties;
};

const baseStyle: React.CSSProperties = {
  fontFamily: FONT.serif,
  color: COLOR.ink,
  lineHeight: 1.6,
  whiteSpace: "nowrap",
};

export const Math: React.FC<MathProps> = ({
  children,
  size = 40,
  block = true,
  style,
}) => {
  const content = (
    <span style={{ ...baseStyle, fontSize: size, ...style }}>{children}</span>
  );
  if (!block) {
    return content;
  }
  return <div style={{ display: "flex", justifyContent: "center" }}>{content}</div>;
};

/** 变量：衬线斜体 */
export const V: React.FC<{ children: React.ReactNode }> = ({ children }) => (
  <span style={{ fontStyle: "italic" }}>{children}</span>
);

/** 向量：粗体衬线 */
export const Vec: React.FC<{ children: React.ReactNode }> = ({ children }) => (
  <span style={{ fontStyle: "italic", fontWeight: 700 }}>{children}</span>
);

/** 下标 */
export const Sub: React.FC<{ children: React.ReactNode }> = ({ children }) => (
  <sub style={{ fontSize: "0.62em", verticalAlign: "-0.25em" }}>{children}</sub>
);

/** 上标 */
export const Sup: React.FC<{ children: React.ReactNode }> = ({ children }) => (
  <sup style={{ fontSize: "0.62em", verticalAlign: "0.5em" }}>{children}</sup>
);

/** 分式 */
export const Frac: React.FC<{
  num: React.ReactNode;
  den: React.ReactNode;
}> = ({ num, den }) => (
  <span
    style={{
      display: "inline-flex",
      flexDirection: "column",
      alignItems: "center",
      verticalAlign: "middle",
      margin: "0 0.18em",
      lineHeight: 1.25,
    }}
  >
    <span style={{ padding: "0 0.3em" }}>{num}</span>
    <span
      style={{
        borderTop: "0.055em solid currentColor",
        width: "100%",
        margin: "0.06em 0",
      }}
    />
    <span style={{ padding: "0 0.3em" }}>{den}</span>
  </span>
);

/** 求和号，上下带累加范围；累加范围由 from/to 指定，不需要子节点 */
export const Sum: React.FC<{
  from?: React.ReactNode;
  to?: string;
}> = ({ from = "j", to }) => (
  <span
    style={{
      display: "inline-flex",
      flexDirection: "column",
      alignItems: "center",
      verticalAlign: "middle",
      margin: "0 0.12em",
      lineHeight: 1.05,
    }}
  >
    {to ? <span style={{ fontSize: "0.5em" }}>{to}</span> : null}
    <span style={{ fontSize: "1.25em", lineHeight: 1 }}>∑</span>
    <span style={{ fontSize: "0.5em" }}>{from}</span>
  </span>
);

/** 省缺部分用省略号 */
export const Dots: React.FC = () => <span style={{ margin: "0 0.2em" }}>…</span>;

/**
 * 公式的入场淡入 + 轻微上浮，避免整块公式硬切。
 */
export const FadeMath: React.FC<
  MathProps & { frame: number; delay?: number; duration?: number }
> = ({ frame, delay = 0, duration = 18, ...rest }) => {
  const progress = interpolate(frame - delay, [0, duration], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });
  return (
    <div
      style={{
        opacity: progress,
        transform: `translateY(${(1 - progress) * 14}px)`,
      }}
    >
      <Math {...rest} />
    </div>
  );
};
