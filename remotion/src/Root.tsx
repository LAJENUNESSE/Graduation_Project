import { Composition } from "remotion";
import { SPHExplainer, TOTAL_DURATION } from "./SPHExplainer";
import { BarChart } from "./BarChart";
import { SAMPLE_DATASET } from "./data";
import { VIDEO } from "./theme";

export const RemotionRoot: React.FC = () => {
  return (
    <>
      {/* 主片：SPH 流体模拟原理讲解 */}
      <Composition
        id="SPHExplainer"
        component={SPHExplainer}
        durationInFrames={TOTAL_DURATION}
        fps={VIDEO.fps}
        width={VIDEO.width}
        height={VIDEO.height}
      />

      {/* 通用柱状图组件，可用于论文插图动画 */}
      <Composition
        id="BarChart"
        component={BarChart}
        durationInFrames={90}
        fps={30}
        width={1600}
        height={900}
        defaultProps={{
          dataset: SAMPLE_DATASET,
          title: "性能对比",
          growDurationInFrames: 40,
        }}
      />
    </>
  );
};
