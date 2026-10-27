/**
 * 输出镜头时间表。
 *
 * 从 SHOTS 数组里提取每个镜头的标题、时长、组件与是否全幅 3D。
 * 解析方式是按「每个镜头的对象」整体切分，而不是按行——
 * 否则 `fullBleed` / `Component` 这类靠后的字段会被切到下一个块里，
 * 导致统计出错（曾出现过「三维镜头 0 个」的错误结论）。
 *
 * 用法：node scripts/shot-timeline.mjs
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const file = path.join(__dirname, "..", "src", "SPHExplainer.tsx");
const source = fs.readFileSync(file, "utf8");

// SHOTS 数组正文
const arrayStart = source.indexOf("export const SHOTS");
if (arrayStart < 0) throw new Error("找不到 SHOTS 数组");
const arrayBody = source.slice(arrayStart);

/**
 * 以对象边界切分镜头：
 * 先按 `index:` 出现位置切，再在每个块里取 duration / title / Component。
 * `index:` 是每个镜头的第一个字段，所以切点最干净。
 */
const startMatch = arrayBody.indexOf("[");
const listBody = arrayBody.slice(startMatch);

// 用 "index:" 作为每个镜头的起点标记
const starts = [];
const idxRe = /\bindex:\s*"/g;
let im;
while ((im = idxRe.exec(listBody)) !== null) {
  starts.push(im.index);
}
if (starts.length === 0) throw new Error("找不到镜头条目");

const rows = [];
let acc = 0;
for (let i = 0; i < starts.length; i++) {
  const from = starts[i];
  const to = i === starts.length - 1 ? listBody.length : starts[i + 1];
  const block = listBody.slice(from, to);

  const durMatch = block.match(/duration:\s*(\d+)/);
  if (!durMatch) continue;

  const titleMatch = block.match(/title:\s*"([^"]*)"/);
  const compMatch = block.match(/Component:\s*([\w.]+)/);
  const fullBleed = /fullBleed:\s*true/.test(block);
  const dur = Number(durMatch[1]);

  rows.push({
    start: acc,
    duration: dur,
    title: titleMatch?.[1] || "",
    component: compMatch?.[1] ?? "?",
    fullBleed,
  });
  acc += dur;
}

for (const r of rows) {
  const mark = r.fullBleed ? "[3D] " : "     ";
  const title = r.title || (r.start === 0 ? "片头" : "结尾");
  console.log(
    `${String(r.start).padStart(5)}  ${String(r.duration).padStart(4)}帧  ${mark}${title}  (${r.component})`,
  );
}

const fps = 30;
console.log(
  `\n镜头数 ${rows.length}，总帧 ${acc}，时长 ${(acc / fps).toFixed(1)} 秒`,
);
console.log(`其中全幅三维镜头 ${rows.filter((r) => r.fullBleed).length} 个`);
