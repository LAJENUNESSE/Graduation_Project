/**
 * 水体外观特征检测。
 *
 * 检查画面里是否真的存在「水」应该有的特征：
 *   1. 主色调集中在蓝青区间（而不是灰、棕等）
 *   2. 有明确的高光（亮度尖峰）
 *   3. 有明暗层次（不是一片平色）
 *
 * 用法：node scripts/check-water.mjs out/shot09.png
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
const at = (x, y) => {
  const i = (y * width + x) * channels;
  return [data[i], data[i + 1], data[i + 2]];
};

let fluidPixels = 0;
let total = 0;
let brightPixels = 0;
const lums = [];

for (let y = 0; y < height; y += 2) {
  for (let x = 0; x < width; x += 2) {
    total++;
    const [r, g, b] = at(x, y);
    const lum = 0.299 * r + 0.587 * g + 0.114 * b;
    lums.push(lum);
    // 水体判据：蓝色明显高于红色
    if (b > r + 18 && b > 60) {
      fluidPixels++;
      if (lum > 185) brightPixels++;
    }
  }
}

lums.sort((a, b) => a - b);
const p = (q) => lums[Math.floor(lums.length * q)];

const result = {
  file,
  fluidRatio: +(fluidPixels / total).toFixed(4),
  highlightRatio: +(brightPixels / Math.max(1, fluidPixels)).toFixed(4),
  luminanceP05: +p(0.05).toFixed(1),
  luminanceP50: +p(0.5).toFixed(1),
  luminanceP95: +p(0.95).toFixed(1),
  luminanceRange: +(p(0.95) - p(0.05)).toFixed(1),
};

console.log(JSON.stringify(result, null, 2));

const problems = [];
if (result.fluidRatio < 0.06) problems.push("水体像素过少，液面可能没占据画面主体");
if (result.luminanceRange < 40) problems.push("明暗层次不足，看起来像一块平色而非立体液面");
if (result.highlightRatio < 0.002) problems.push("缺少高光，水体缺乏光泽感");

if (problems.length) {
  console.log("\n判定：外观特征不足");
  problems.forEach((p2) => console.log("  - " + p2));
  process.exitCode = 1;
} else {
  console.log("\n判定：具备水体特征（成片蓝青主色、有高光、有明暗层次）。");
}
