/**
 * 液面连续性检测。
 *
 * 「一堆小球」和「连续液面」在像素上的区别很明确：
 * 球堆的边缘是大量小圆弧、相邻像素剧烈跳变；
 * 连续液面的边缘平滑，梯度分布集中。
 *
 * 这里统计图像的水平梯度分布：如果出现大量高频跳变，
 * 说明画面仍是颗粒状，而不是光滑液面。
 *
 * 用法：node scripts/check-smoothness.mjs out/fl3.png
 */
import fs from "node:fs";
import zlib from "node:zlib";

const decodePng = (buffer) => {
  let pos = 8;
  let width = 0;
  let height = 0;
  let colorType = 0;
  const idat = [];
  while (pos < buffer.length) {
    const length = buffer.readUInt32BE(pos);
    const type = buffer.toString("ascii", pos + 4, pos + 8);
    const data = buffer.subarray(pos + 8, pos + 8 + length);
    if (type === "IHDR") {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      colorType = data[9];
    } else if (type === "IDAT") idat.push(data);
    else if (type === "IEND") break;
    pos += 12 + length;
  }
  const channels = colorType === 6 ? 4 : 3;
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const out = Buffer.alloc(width * height * channels);
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)];
    const line = raw.subarray(y * (stride + 1) + 1, y * (stride + 1) + 1 + stride);
    const cur = out.subarray(y * stride, (y + 1) * stride);
    const prev = y > 0 ? out.subarray((y - 1) * stride, y * stride) : null;
    for (let x = 0; x < stride; x++) {
      const a = x >= channels ? cur[x - channels] : 0;
      const b = prev ? prev[x] : 0;
      const c = prev && x >= channels ? prev[x - channels] : 0;
      let v = line[x];
      if (filter === 1) v += a;
      else if (filter === 2) v += b;
      else if (filter === 3) v += (a + b) >> 1;
      else if (filter === 4) {
        const p = a + b - c;
        const pa = Math.abs(p - a);
        const pb = Math.abs(p - b);
        const pc = Math.abs(p - c);
        v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      cur[x] = v & 0xff;
    }
  }
  return { width, height, channels, data: out };
};

const file = process.argv[2];
const img = decodePng(fs.readFileSync(file));
const { width, height, channels, data } = img;

const lum = (x, y) => {
  const i = (y * width + x) * channels;
  return 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
};

// 逐像素统计相邻亮度差（不跳采样，否则小球的细小边缘会被漏掉）
const diffs = [];
for (let y = 1; y < height - 1; y++) {
  for (let x = 1; x < width - 2; x++) {
    diffs.push(Math.abs(lum(x + 1, y) - lum(x, y)));
  }
}

diffs.sort((a, b) => a - b);
const pct = (p) => diffs[Math.floor(diffs.length * p)] ?? 0;
const mean = diffs.reduce((a, b) => a + b, 0) / diffs.length;

// 高频跳变比例：差值 > 40 的像素占比
const highFreq = diffs.filter((d) => d > 40).length / diffs.length;
// 中高跳变：小球轮廓主要由 15~40 的梯度构成
const midFreq = diffs.filter((d) => d > 15 && d <= 40).length / diffs.length;

const result = {
  file,
  meanGradient: +mean.toFixed(2),
  p50: +pct(0.5).toFixed(2),
  p95: +pct(0.95).toFixed(2),
  p99: +pct(0.99).toFixed(2),
  highFreqRatio: +highFreq.toFixed(4),
  midFreqRatio: +midFreq.toFixed(4),
};

console.log(JSON.stringify(result, null, 2));

// 颗粒状小球堆的边缘会产生大量高频跳变
if (highFreq > 0.06) {
  console.log("\n判定：画面仍有明显颗粒感（高频边缘过多），不像连续液面。");
  process.exitCode = 1;
} else {
  console.log("\n判定：梯度分布集中，边缘平滑，符合连续液面特征。");
}
