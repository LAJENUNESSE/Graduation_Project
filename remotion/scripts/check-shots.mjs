/**
 * 镜头边界与内容分布的联合体检。
 *
 * check-pixels.mjs 单独看会发现「空白画面」，但那些其实都是镜头开头的
 * 淡入起始帧。这里把镜头时间表和缩略图对上，只把**镜头中段**仍然空白
 * 的画面判为缺陷，同时输出每个镜头的有效内容占比，便于一眼看出哪个
 * 镜头信息密度过低。
 *
 * 用法：node scripts/check-shots.mjs
 */
import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(__dirname, "..");
const shotsDir = path.join(root, "out", "shots");

/** 与 src/SPHExplainer.tsx 的 SHOTS 时长保持一致 */
const SHOT_TABLE = [
  ["片头", 150],
  ["01 拉格朗日离散", 400],
  ["02 SPH 母式", 420],
  ["03 核函数", 450],
  ["04 密度与状态方程", 450],
  ["05 WCSPH 力模型", 480],
  ["06 邻居搜索", 480],
  ["07 PCISPH 迭代", 540],
  ["08 δ 与网格重建", 420],
  ["09 稳定化措施", 450],
  ["10 参数三层结构", 480],
  ["11 GPU 数据布局", 450],
  ["12 双后端一致性", 420],
  ["13 实测性能", 480],
  ["14 求解器取舍", 420],
  ["15 失败模式", 420],
  ["16 结尾", 300],
];

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

const contentRatio = (img) => {
  const { width, height, channels, data } = img;
  const at = (x, y) => {
    const i = (y * width + x) * channels;
    return [data[i], data[i + 1], data[i + 2]];
  };
  const bg = at(0, 0);
  let content = 0;
  let total = 0;
  for (let y = 0; y < height; y += 2) {
    for (let x = 0; x < width; x += 2) {
      total++;
      const p = at(x, y);
      if (
        Math.abs(p[0] - bg[0]) >= 10 ||
        Math.abs(p[1] - bg[1]) >= 10 ||
        Math.abs(p[2] - bg[2]) >= 10
      ) {
        content++;
      }
    }
  }
  return content / total;
};

const files = fs
  .readdirSync(shotsDir)
  .filter((f) => f.startsWith("f") && f.endsWith(".png"))
  .sort();

const boundsOf = (frame) => {
  let acc = 0;
  for (const [name, dur] of SHOT_TABLE) {
    if (frame >= acc && frame < acc + dur) {
      return { name, start: acc, local: frame - acc, duration: dur };
    }
    acc += dur;
  }
  return null;
};

const report = [];
const defects = [];

for (const f of files) {
  const frame = parseInt(f.slice(1, 6), 10);
  const img = decodePng(fs.readFileSync(path.join(shotsDir, f)));
  const ratio = contentRatio(img);
  const where = boundsOf(frame);
  const entry = {
    frame,
    shot: where?.name ?? "?",
    localFrame: where?.local ?? -1,
    ratio: +ratio.toFixed(4),
  };
  report.push(entry);

  // 只在镜头中段判缺陷：开头 40 帧是淡入，结尾 20 帧是淡出/切镜
  const inMiddle =
    where && where.local > 40 && where.local < where.duration - 20;
  if (inMiddle && ratio < 0.02) {
    defects.push(entry);
  }
}

console.log("每个镜头的内容占比（越低说明该镜头越空）：");
const byShot = new Map();
for (const r of report) {
  if (!byShot.has(r.shot)) byShot.set(r.shot, []);
  byShot.get(r.shot).push(r.ratio);
}
for (const [shot, ratios] of byShot) {
  const avg = ratios.reduce((a, b) => a + b, 0) / ratios.length;
  const min = Math.min(...ratios);
  console.log(
    `  ${shot.padEnd(22)} 采样 ${String(ratios.length).padStart(2)} 帧，平均 ${(avg * 100).toFixed(1).padStart(5)}%，最低 ${(min * 100).toFixed(1).padStart(5)}%`,
  );
}

if (defects.length === 0) {
  console.log("\n镜头中段检查通过：无空白画面。");
} else {
  console.log("\n以下镜头中段出现空白画面：");
  console.log(JSON.stringify(defects, null, 2));
  process.exitCode = 1;
}
