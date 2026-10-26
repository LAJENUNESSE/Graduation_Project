/**
 * 全片视觉常量。
 *
 * 浅色底 + 深色字，投影仪/PPT 环境下比深色主题更稳；
 * 配色沿用论文配图的十六进制值，保证视频与文档一致。
 */

export const COLOR = {
  bg: "#FAFAF7",
  bgAlt: "#F1F0EA",
  ink: "#1A1A1A",
  inkSoft: "#4A4A4A",
  inkMuted: "#7A7A7A",
  rule: "#D8D6CC",
  accent: "#2E6FA8",
  accentSoft: "#DCEEF8",
  orange: "#C4661E",
  orangeSoft: "#FBE6D4",
  green: "#2E8B6F",
  greenSoft: "#DDF3EA",
  purple: "#7A3E9D",
  purpleSoft: "#EDE2F5",
  red: "#B03A2E",
  redSoft: "#F7DEDB",
} as const;

/** 中文用无衬线，数学量与公式用衬线斜体，接近论文排版。 */
export const FONT = {
  sans: '"Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC", sans-serif',
  serif: '"Times New Roman", "Songti SC", "SimSun", serif',
  mono: '"Cascadia Mono", "Consolas", "Courier New", monospace',
} as const;

export const VIDEO = {
  width: 1920,
  height: 1080,
  fps: 30,
} as const;

/** 画面安全边距 */
export const PAD = 96;
