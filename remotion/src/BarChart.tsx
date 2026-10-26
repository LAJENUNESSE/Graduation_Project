import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import type { Dataset } from "./data";
import { SAMPLE_DATASET } from "./data";

type Props = {
  /** 数据源，默认使用示例数据，可通过 Studio 右侧 Input Props 覆盖 */
  dataset?: Dataset;
  /** 图表标题 */
  title?: string;
  /** 柱子从 0 长到目标高度所用的帧数 */
  growDurationInFrames?: number;
};

const FONT_STACK =
  '"Times New Roman", "SimSun", "Songti SC", "Noto Serif CJK SC", serif';

/**
 * 带入场动画的柱状图。
 *
 * 动画只依赖帧号，因此渲染结果完全确定，同一份代码导出多少遍都一致。
 */
export const BarChart: React.FC<Props> = ({
  dataset = SAMPLE_DATASET,
  title = "性能对比",
  growDurationInFrames = 40,
}) => {
  const frame = useCurrentFrame();

  const maxValue = Math.max(
    ...dataset.series.flatMap((series) => series.values),
  );
  const axisMax = maxValue * 1.15;
  const tickCount = 5;
  const ticks = Array.from(
    { length: tickCount + 1 },
    (_, index) => (axisMax / tickCount) * index,
  );

  const formatTick = (value: number) =>
    Number.isInteger(value) ? String(value) : value.toFixed(1);

  return (
    <AbsoluteFill
      style={{
        backgroundColor: "#FFFFFF",
        fontFamily: FONT_STACK,
        color: "#1A1A1A",
        padding: "48px 64px",
      }}
    >
      <div style={{ fontSize: 34, fontWeight: 600, marginBottom: 8 }}>
        {title}
      </div>
      <div style={{ fontSize: 18, color: "#666666", marginBottom: 24 }}>
        纵轴单位：{dataset.unit}
      </div>

      <div
        style={{
          position: "relative",
          flex: 1,
          display: "flex",
          borderLeft: "2px solid #333333",
          borderBottom: "2px solid #333333",
        }}
      >
        {/* 水平网格线与刻度 */}
        {ticks.map((tick, index) => {
          const bottom = (tick / axisMax) * 100;
          return (
            <div
              key={tick}
              style={{
                position: "absolute",
                left: 0,
                right: 0,
                bottom: `${bottom}%`,
                borderTop:
                  index === 0 ? "none" : "1px dashed rgba(0, 0, 0, 0.15)",
              }}
            >
              <span
                style={{
                  position: "absolute",
                  right: "100%",
                  top: -12,
                  paddingRight: 10,
                  fontSize: 16,
                  color: "#666666",
                  whiteSpace: "nowrap",
                }}
              >
                {formatTick(tick)}
              </span>
            </div>
          );
        })}

        {/* 分组柱体：同一类别下的每个系列并排 */}
        <div
          style={{
            display: "flex",
            alignItems: "flex-end",
            justifyContent: "space-around",
            width: "100%",
            height: "100%",
            paddingBottom: 0,
          }}
        >
          {dataset.labels.map((label, categoryIndex) => (
            <div
              key={label}
              style={{
                display: "flex",
                flexDirection: "column",
                alignItems: "center",
                justifyContent: "flex-end",
                height: "100%",
                flex: 1,
              }}
            >
              <div
                style={{
                  display: "flex",
                  alignItems: "flex-end",
                  gap: 6,
                  height: "100%",
                  width: "100%",
                  justifyContent: "center",
                }}
              >
                {dataset.series.map((series, seriesIndex) => {
                  const value = series.values[categoryIndex];
                  // 每个系列错开一点起始帧，形成逐个生长的节奏
                  const delay = seriesIndex * 4;
                  const progress = interpolate(
                    frame - delay,
                    [0, growDurationInFrames],
                    [0, 1],
                    {
                      extrapolateLeft: "clamp",
                      extrapolateRight: "clamp",
                      easing: (t: number) => 1 - Math.pow(1 - t, 3),
                    },
                  );
                  const heightPercent = (value / axisMax) * 100 * progress;

                  return (
                    <div
                      key={series.name}
                      style={{
                        position: "relative",
                        width: 42,
                        height: `${heightPercent}%`,
                        backgroundColor: series.color,
                        borderRadius: "3px 3px 0 0",
                      }}
                    >
                      <span
                        style={{
                          position: "absolute",
                          bottom: "100%",
                          left: "50%",
                          transform: "translate(-50%, -4px)",
                          fontSize: 16,
                          color: "#333333",
                          opacity: progress,
                          whiteSpace: "nowrap",
                        }}
                      >
                        {value.toFixed(1)}
                      </span>
                    </div>
                  );
                })}
              </div>
            </div>
          ))}
        </div>

        {/* 横轴标签放在边框下方 */}
        <div
          style={{
            position: "absolute",
            top: "100%",
            left: 0,
            right: 0,
            display: "flex",
            paddingTop: 10,
          }}
        >
          {dataset.labels.map((label) => (
            <div
              key={label}
              style={{
                flex: 1,
                textAlign: "center",
                fontSize: 18,
                color: "#333333",
              }}
            >
              {label}
            </div>
          ))}
        </div>
      </div>

      {/* 图例 */}
      <div
        style={{
          display: "flex",
          gap: 24,
          justifyContent: "center",
          marginTop: 56,
          fontSize: 18,
        }}
      >
        {dataset.series.map((series) => (
          <div
            key={series.name}
            style={{ display: "flex", alignItems: "center", gap: 8 }}
          >
            <span
              style={{
                width: 16,
                height: 16,
                borderRadius: 3,
                backgroundColor: series.color,
                display: "inline-block",
              }}
            />
            <span>{series.name}</span>
          </div>
        ))}
      </div>
    </AbsoluteFill>
  );
};
