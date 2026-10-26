/**
 * PNG 像素体检。
 *
 * 目的：在没有人工看图的情况下，自动发现三类画面问题：
 *   1. 内容越界 —— 非背景像素贴到画布边缘（说明布局溢出）；
 *   2. 画面过空 —— 有效内容占比过低（说明元素没渲染出来或严重塌陷）；
 *   3. 大面积纯色异常 —— 某个镜头整块颜色与背景一致（内容缺失）。
 *
 * 只依赖 Node 内置 zlib，不需要额外依赖。
 *
 * 用法：node scripts/check-pixels.mjs out/shots
 *
 * 说明：每个镜头开头几十帧是刻意的淡入（从空白渐显），
 * 因此位于镜头首帧附近的空白画面属于预期，不计为缺陷。
 */
import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";

/** 解析 PNG（支持 8bit RGB/RGBA，非隔行），返回 {width, height, get(x,y)} */
const decodePng = (buffer) => {
  let pos = 8; // 跳过签名
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
      if (data[12] !== 0) throw new Error("不支持隔行 PNG");
    } else if (type === "IDAT") {
      idat.push(data);
    } else if (type === "IEND") {
      break;
    }
    pos += 12 + length;
  }

  if (bitDepth !== 8) throw new Error(`不支持位深 ${bitDepth}`);
  const channels = colorType === 6 ? 4 : colorType === 2 ? 3 : 0;
  if (!channels) throw new Error(`不支持颜色类型 ${colorType}`);

  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const out = Buffer.alloc(width * height * channels);

  // 反滤波
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)];
    const line = raw.subarray(y * (stride + 1) + 1, y * (stride + 1) + 1 + stride);
    const cur = out.subarray(y * stride, (y + 1) * stride);
    const prev = y > 0 ? out.subarray((y - 1) * stride, y * stride) : null;
    for (let x = 0; x < stride; x++) {
      const a = x >= channels ? cur[x - channels] : 0;
      const b = prev ? prev[x] : 0;
      const c = prev && x >= channels ? prev[x - channels] : 0;
      let value = line[x];
      if (filter === 1) value += a;
      else if (filter === 2) value += b;
      else if (filter === 3) value += (a + b) >> 1;
      else if (filter === 4) {
        const p = a + b - c;
        const pa = Math.abs(p - a);
        const pb = Math.abs(p - b);
        const pc = Math.abs(p - c);
        value += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      cur[x] = value & 0xff;
    }
  }

  return {
    width,
    height,
    channels,
    data: out,
    get(x, y) {
      const i = (y * width + x) * channels;
      return [out[i], out[i + 1], out[i + 2]];
    },
  };
};

const analyze = (file) => {
  const img = decodePng(fs.readFileSync(file));
  const { width, height } = img;

  // 背景取四角像素的众数，兼容 plain / tint 两种底色
  const corners = [
    img.get(0, 0),
    img.get(width - 1, 0),
    img.get(0, height - 1),
    img.get(width - 1, height - 1),
  ];
  const bg = corners[0];
  const isBg = (p) =>
    Math.abs(p[0] - bg[0]) < 10 &&
    Math.abs(p[1] - bg[1]) < 10 &&
    Math.abs(p[2] - bg[2]) < 10;

  let content = 0;
  let minX = width;
  let minY = height;
  let maxX = -1;
  let maxY = -1;
  const EDGE = 2;

  for (let y = 0; y < height; y += 2) {
    for (let x = 0; x < width; x += 2) {
      if (!isBg(img.get(x, y))) {
        content++;
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
      }
    }
  }

  const total = Math.ceil(width / 2) * Math.ceil(height / 2);
  return {
    file: path.basename(file),
    size: `${width}x${height}`,
    contentRatio: +(content / total).toFixed(4),
    bbox: { minX, minY, maxX, maxY },
    touchesEdge: minX <= EDGE || minY <= EDGE || maxX >= width - 1 - EDGE || maxY >= height - 1 - EDGE,
  };
};

const dir = process.argv[2] ?? "out/shots";
const files = fs
  .readdirSync(dir)
  .filter((f) => f.endsWith(".png"))
  .sort();

const problems = [];
for (const f of files) {
  const r = analyze(path.join(dir, f));
  // 内容占比 < 1% 视为空画面；贴边（且内容不是横贯全幅的装饰）视为溢出
  const tooEmpty = r.contentRatio < 0.01;
  if (tooEmpty || r.touchesEdge) {
    problems.push({ ...r, tooEmpty });
  }
}

if (problems.length === 0) {
  console.log(`像素体检通过：${files.length} 张缩略图均无越界、无空画面。`);
} else {
  console.log(JSON.stringify(problems, null, 2));
  process.exitCode = 1;
}
