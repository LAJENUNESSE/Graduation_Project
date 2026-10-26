/**
 * 卡死帧检测。
 *
 * 从成片里等间隔抽帧，比较相邻抽样帧的像素差异。
 * 如果连续若干秒画面完全不变，说明动画卡住或该段是死帧——
 * 这是「渲染成功」但成片不可用的典型症状，靠看日志发现不了。
 *
 * 用法：node scripts/check-motion.mjs out/SPH-Principles-3D.mp4
 */
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(__dirname, "..");
const ffmpeg = path.join(
  root,
  "node_modules",
  "@remotion",
  "compositor-win32-x64-msvc",
  "ffmpeg.exe",
);

const video = process.argv[2] ?? path.join(root, "out", "SPH-Principles-3D.mp4");
const tmp = path.join(root, "out", "motion-check");
fs.rmSync(tmp, { recursive: true, force: true });
fs.mkdirSync(tmp, { recursive: true });

// 每 2 秒抽一帧，缩到 320x180。
// 注意：Remotion 自带的精简版 ffmpeg 只编了 scale 滤镜，没有 fps/select，
// 因此不能用 -vf fps=...，改用输出帧率 -r 控制抽样密度。
const outPattern = path.join(tmp, "f%04d.png");
execFileSync(
  ffmpeg,
  [
    "-v",
    "error",
    "-i",
    video,
    "-vf",
    "scale=320:180",
    "-r",
    "0.5",
    "-y",
    outPattern,
  ],
  { stdio: "inherit" },
);

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

const files = fs
  .readdirSync(tmp)
  .filter((f) => f.endsWith(".png"))
  .sort();

const decoded = files.map((f) => decodePng(fs.readFileSync(path.join(tmp, f))));

/** 两帧的平均绝对像素差（0-255） */
const diff = (a, b) => {
  if (!a || !b) return 255;
  let sum = 0;
  const n = Math.min(a.data.length, b.data.length);
  for (let i = 0; i < n; i += 7) {
    sum += Math.abs(a.data[i] - b.data[i]);
  }
  return sum / Math.ceil(n / 7);
};

console.log(`抽样 ${decoded.length} 帧（每 2 秒一帧）`);

const frozen = [];
for (let i = 1; i < decoded.length; i++) {
  const d = diff(decoded[i - 1], decoded[i]);
  if (d < 0.35) {
    frozen.push({ atSecond: i * 2, diff: +d.toFixed(3) });
  }
}

if (frozen.length === 0) {
  console.log("未发现画面卡死：相邻抽样帧均有可见变化。");
} else {
  console.log("以下时间点画面几乎无变化（可能卡死或静态过久）：");
  console.log(JSON.stringify(frozen, null, 2));
  // 全片是讲解视频，短暂静止是正常的；只有连续 3 个抽样点（6 秒）不动才算问题
  const runs = [];
  let run = [frozen[0]];
  for (let i = 1; i < frozen.length; i++) {
    if (frozen[i].atSecond - frozen[i - 1].atSecond === 2) run.push(frozen[i]);
    else {
      runs.push(run);
      run = [frozen[i]];
    }
  }
  runs.push(run);
  const bad = runs.filter((r) => r.length >= 3);
  if (bad.length) {
    console.log(`\n判定：失败，存在 ${bad.length} 段连续 ≥6 秒无变化。`);
    process.exitCode = 1;
  } else {
    console.log("\n判定：通过，静止段均为短暂停留（< 6 秒）。");
  }
}

fs.rmSync(tmp, { recursive: true, force: true });
