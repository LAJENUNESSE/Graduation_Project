/**
 * 3D 画面有效性验证。
 *
 * 「渲染没报错」不等于「画面里真有立体几何」：headless 软件光栅化下
 * WebGL 上下文可能创建成功但整屏全黑或只有背景色。这里直接统计
 * 非背景像素的数量与颜色分布，确认确实画出了几何体。
 *
 * 用法：node scripts/check-3d.mjs out/probe.png
 */
import fs from "node:fs";
import zlib from "node:zlib";

const decodePng = (buffer) => {
  let pos = 8;
  let width = 0;
  let height = 0;
  let bitDepth = 0;
  let colorType = 0;
  const idat = [];
  while (pos < buffer.length) {
    const length = buffer.readUInt32BE(pos);
    const type = buffer.toString("ascii", pos + 4, pos + 8);
    const data = buffer.subarray(pos + 8, pos + 8 + length);
    if (type === "IHDR") {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      bitDepth = data[8];
      colorType = data[9];
    } else if (type === "IDAT") idat.push(data);
    else if (type === "IEND") break;
    pos += 12 + length;
  }
  if (bitDepth !== 8) throw new Error(`不支持的位深 ${bitDepth}`);
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

const file = process.argv[2] ?? "out/probe.png";
const img = decodePng(fs.readFileSync(file));
const { width, height, channels, data } = img;
const at = (x, y) => {
  const i = (y * width + x) * channels;
  return [data[i], data[i + 1], data[i + 2]];
};

// 背景取左上角
const bg = at(0, 0);
const uniqueColors = new Set();
let nonBg = 0;
let total = 0;
let sumR = 0;
let sumG = 0;
let sumB = 0;
let litSamples = 0;

for (let y = 0; y < height; y += 2) {
  for (let x = 0; x < width; x += 2) {
    total++;
    const p = at(x, y);
    if (
      Math.abs(p[0] - bg[0]) > 12 ||
      Math.abs(p[1] - bg[1]) > 12 ||
      Math.abs(p[2] - bg[2]) > 12
    ) {
      nonBg++;
      sumR += p[0];
      sumG += p[1];
      sumB += p[2];
      if (p[0] + p[1] + p[2] > 90) litSamples++;
      // 量化到 8 级，统计色彩多样性
      uniqueColors.add(
        `${p[0] >> 5},${p[1] >> 5},${p[2] >> 5}`,
      );
    }
  }
}

const ratio = nonBg / total;
const result = {
  file,
  size: `${width}x${height}`,
  background: `rgb(${bg.join(",")})`,
  nonBackgroundRatio: +ratio.toFixed(4),
  distinctColorBuckets: uniqueColors.size,
  meanBrightness: nonBg ? Math.round((sumR + sumG + sumB) / (nonBg * 3)) : 0,
  litSampleCount: litSamples,
};

console.log(JSON.stringify(result, null, 2));

// 判定：非背景像素过少 = 没画出东西；色彩桶过少 = 只有纯色块；
// 平均亮度过低 = 全黑或几乎全黑
const problems = [];
if (result.nonBackgroundRatio < 0.02) {
  problems.push("非背景像素过少，3D 内容可能没有渲染出来");
}
if (result.distinctColorBuckets < 4) {
  problems.push("颜色种类过少，可能只有纯色背景或单个色块");
}
if (result.meanBrightness < 20) {
  problems.push("画面平均亮度过低，可能是全黑");
}

if (problems.length) {
  console.log("\n判定：失败");
  problems.forEach((p) => console.log("  - " + p));
  process.exitCode = 1;
} else {
  console.log("\n判定：通过，画面中确实存在立体的、带明暗变化的几何内容。");
}
