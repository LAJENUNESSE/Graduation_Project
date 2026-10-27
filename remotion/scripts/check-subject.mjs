/**
 * 通用「画面主体占比」检测。
 *
 * check-water.mjs 用「蓝色高于红色」判液体，只适用于蓝色水。
 * 有的镜头故意用密度热力色（偏红），判据会失效并误报「几乎没水」。
 *
 * 这里改用与色调无关的方式：统计与背景差异明显的像素占比，
 * 只要主体占得住画面，就说明镜头构图正常。
 *
 * 用法：node scripts/check-subject.mjs out/shots/f03370.png
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

const files = process.argv.slice(2);
if (files.length === 0) {
  console.error("用法：node scripts/check-subject.mjs a.png b.png ...");
  process.exit(1);
}

for (const file of files) {
  const img = decodePng(fs.readFileSync(file));
  const { width, height, channels, data } = img;
  const at = (x, y) => {
    const i = (y * width + x) * channels;
    return [data[i], data[i + 1], data[i + 2]];
  };

  // 背景取角落众数中第一个足够亮的像素
  const bg = at(2, 2);

  let subject = 0;
  let total = 0;
  let satSum = 0;

  for (let y = 0; y < height; y += 2) {
    for (let x = 0; x < width; x += 2) {
      total++;
      const [r, g, b] = at(x, y);
      const dist =
        Math.abs(r - bg[0]) + Math.abs(g - bg[1]) + Math.abs(b - bg[2]);
      if (dist > 40) {
        subject++;
        const mx = Math.max(r, g, b);
        const mn = Math.min(r, g, b);
        satSum += mx - mn;
      }
    }
  }

  const ratio = subject / total;
  const avgSat = subject ? satSum / subject : 0;
  console.log(
    `${file.padEnd(34)} 主体占比 ${(ratio * 100).toFixed(1).padStart(5)}%   平均饱和度 ${avgSat.toFixed(0).padStart(3)}`,
  );
}
