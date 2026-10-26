import { Composition } from "remotion";
import { SPHExplainer, TOTAL_DURATION } from "./SPHExplainer";
import { BarChart } from "./BarChart";
import { SAMPLE_DATASET } from "./data";
import { VIDEO } from "./theme";
import { ThreeProbe } from "./scenes/ThreeProbe";
import { HashGrid3D, KernelSphere3D } from "./scenes/three/Neighbor3D";
import { FluidColumn3D, PCISPH3D } from "./scenes/three/PCISPH3D";

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

      {/* 3D 能力探针：验证 headless 软件 WebGL 是否可用 */}
      <Composition
        id="ThreeProbe"
        component={ThreeProbe}
        durationInFrames={90}
        fps={30}
        width={VIDEO.width}
        height={VIDEO.height}
      />

      {/* 3D 分镜组件，供主片引用，也可单独预览 */}
      <Composition
        id="KernelSphere3D"
        component={KernelSphere3D}
        durationInFrames={360}
        fps={30}
        width={VIDEO.width}
        height={VIDEO.height}
      />
      <Composition
        id="HashGrid3D"
        component={HashGrid3D}
        durationInFrames={420}
        fps={30}
        width={VIDEO.width}
        height={VIDEO.height}
      />
      <Composition
        id="PCISPH3D"
        component={PCISPH3D}
        durationInFrames={380}
        fps={30}
        width={VIDEO.width}
        height={VIDEO.height}
      />
      <Composition
        id="FluidColumn3D"
        component={FluidColumn3D}
        durationInFrames={380}
        fps={30}
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
