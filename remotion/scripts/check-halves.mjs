/**
 * 把截图左右对半分并分别统计非背景像素，用于 A/B 对照判断。
 *
 * 用法：node scripts/check-halves.mjs out/mc8.png
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
const at = (x, y) => {
  const i = (y * img.width + x) * img.channels;
  return [img.data[i], img.data[i + 1], img.data[i + 2]];
};
const bg = at(2, 2);

const stats = (x0, x1) => {
  let non = 0;
  let total = 0;
  const colors = new Set();
  for (let y = 0; y < img.height; y += 2) {
    for (let x = x0; x < x1; x += 2) {
      total++;
      const p = at(x, y);
      if (
        Math.abs(p[0] - bg[0]) > 12 ||
        Math.abs(p[1] - bg[1]) > 12 ||
        Math.abs(p[2] - bg[2]) > 12
      ) {
        non++;
        colors.add(`${p[0] >> 5},${p[1] >> 5},${p[2] >> 5}`);
      }
    }
  }
  return {
    ratio: +(non / total).toFixed(4),
    colors: colors.size,
  };
};

const mid = Math.floor(img.width / 2);
console.log(JSON.stringify({
  file,
  background: `rgb(${bg.join(",")})`,
  leftHalf: stats(0, mid),
  rightHalf: stats(mid, img.width),
}, null, 2));
