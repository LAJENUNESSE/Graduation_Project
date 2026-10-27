/**
 * 从截图里裁出左上角的诊断文字区域，另存为小图，
 * 方便用像素统计判断「文字是否渲染出来」，以及区分球体与等值面。
 *
 * 用法：node scripts/crop-region.mjs in.png out.png x y w h
 */
import fs from "node:fs";
import zlib from "node:zlib";

const crcTable = (() => {
  const t = new Int32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c;
  }
  return t;
})();

const crc32 = (buf) => {
  let c = -1;
  for (let i = 0; i < buf.length; i++) c = crcTable[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ -1) >>> 0;
};

const chunk = (type, data) => {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const typeBuf = Buffer.from(type, "ascii");
  const body = Buffer.concat([typeBuf, data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([len, body, crc]);
};

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

const [, , inFile, outFile, xs, ys, ws, hs] = process.argv;
const x0 = Number(xs);
const y0 = Number(ys);
const w = Number(ws);
const h = Number(hs);

const img = decodePng(fs.readFileSync(inFile));
const src = img.data;
const dst = Buffer.alloc(w * h * img.channels);

for (let y = 0; y < h; y++) {
  for (let x = 0; x < w; x++) {
    const sx = Math.min(img.width - 1, x0 + x);
    const sy = Math.min(img.height - 1, y0 + y);
    const si = (sy * img.width + sx) * img.channels;
    const di = (y * w + x) * img.channels;
    for (let c = 0; c < img.channels; c++) dst[di + c] = src[si + c];
  }
}

// 重新编码成 PNG（filter 0）
const rawOut = Buffer.alloc(h * (w * img.channels + 1));
for (let y = 0; y < h; y++) {
  rawOut[y * (w * img.channels + 1)] = 0;
  dst.copy(
    rawOut,
    y * (w * img.channels + 1) + 1,
    y * w * img.channels,
    (y + 1) * w * img.channels,
  );
}

const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(w, 0);
ihdr.writeUInt32BE(h, 4);
ihdr[8] = 8;
ihdr[9] = img.channels === 4 ? 6 : 2;
const png = Buffer.concat([
  Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
  chunk("IHDR", ihdr),
  chunk("IDAT", zlib.deflateSync(rawOut)),
  chunk("IEND", Buffer.alloc(0)),
]);
fs.writeFileSync(outFile, png);
console.log(`已裁剪 ${w}x${h} 区域到 ${outFile}`);
