import { Composition } from "remotion";
import { BarChart } from "./BarChart";
import { SAMPLE_DATASET } from "./data";

export const RemotionRoot: React.FC = () => {
  return (
    <>
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
