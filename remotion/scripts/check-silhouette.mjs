/**
 * 轮廓粗糙度检测：区分「球堆」与「连续液面」。
 *
 * 原理：
 *   球堆的水平轮廓由一串小圆弧拼成，相邻行的边界位置会来回跳动；
 *   光线步进求出的等值面是光滑曲面，轮廓的二阶差分应当很小。
 *
 * 具体做法：逐行取最左/最右的液体像素得到轮廓曲线，
 * 统计该曲线的二阶差分绝对值均值（曲率抖动）。数值越小越光滑。
 *
 * 用法：node scripts/check-silhouette.mjs a.png b.png ...
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

/**
 * 找出「液体像素」。
 *
 * 液体判据：偏蓝色相（b 明显大于 r）。
 * 这样既不依赖背景色，也不会把地面与 HUD 算进去。
 */
const isFluid = (r, g, b) => b > r + 18 && b > 60;

const analyze = (file) => {
  const img = decodePng(fs.readFileSync(file));
  const { width, height, channels, data } = img;
  const at = (x, y) => {
    const i = (y * width + x) * channels;
    return [data[i], data[i + 1], data[i + 2]];
  };

  const left = [];
  const right = [];
  for (let y = 0; y < height; y++) {
    let l = -1;
    let r = -1;
    for (let x = 0; x < width; x++) {
      const [rr, gg, bb] = at(x, y);
      if (isFluid(rr, gg, bb)) {
        if (l < 0) l = x;
        r = x;
      }
    }
    if (l >= 0 && r - l > 8) {
      left.push(l);
      right.push(r);
    }
  }

  if (left.length < 20) {
    return { file, rows: left.length, error: "液体像素太少，无法评估轮廓" };
  }

  // 二阶差分：轮廓曲率的抖动程度
  const roughness = (arr) => {
    let sum = 0;
    let n = 0;
    for (let i = 2; i < arr.length; i++) {
      const d2 = arr[i] - 2 * arr[i - 1] + arr[i - 2];
      sum += Math.abs(d2);
      n++;
    }
    return n ? sum / n : 0;
  };

  const rl = roughness(left);
  const rr = roughness(right);

  return {
    file,
    rows: left.length,
    leftRoughness: +rl.toFixed(3),
    rightRoughness: +rr.toFixed(3),
    meanRoughness: +((rl + rr) / 2).toFixed(3),
  };
};

const files = process.argv.slice(2);
if (files.length === 0) {
  console.error("用法：node scripts/check-silhouette.mjs a.png b.png ...");
  process.exit(1);
}

const results = files.map(analyze);
console.log(JSON.stringify(results, null, 2));

const valid = results.filter((r) => !r.error);
if (valid.length >= 2) {
  const sorted = [...valid].sort((a, b) => a.meanRoughness - b.meanRoughness);
  console.log("\n轮廓最光滑 → 最粗糙：");
  for (const r of sorted) {
    console.log(`  ${r.meanRoughness.toFixed(3).padStart(8)}  ${r.file}`);
  }
  console.log("\n说明：数值越小，轮廓越接近光滑等值面；越大越像一堆独立球体的拼接。");
}
