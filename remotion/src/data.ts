/**
 * 数据源定义。
 *
 * 这里的示例数据是占位用的，真实使用时替换成从 benchmark 结果里导出的数值，
 * 例如 benchmark/results 下的 JSON，或用脚本生成同结构的 TS 文件。
 */

export type Dataset = {
  /** 横轴标签，例如各个场景或类别 */
  labels: string[];
  /** 每个系列对应 labels 的一组数值，长度必须与 labels 一致 */
  series: {
    name: string;
    /** 与系列绑定的固定颜色，保证图表与论文里的配色一致 */
    color: string;
    values: number[];
  }[];
  /** 纵轴单位，会拼在刻度后面显示 */
  unit: string;
};

export const SAMPLE_DATASET: Dataset = {
  labels: ["基线", "优化 A", "优化 B", "本文方法"],
  series: [
    {
      name: "帧耗时",
      color: "#2E6FA8",
      values: [16.8, 12.4, 9.7, 6.3],
    },
    {
      name: "仿真耗时",
      color: "#C4661E",
      values: [11.2, 8.9, 6.1, 3.8],
    },
  ],
  unit: "ms",
};
