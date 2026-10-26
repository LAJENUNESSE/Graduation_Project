/**
 * 输出镜头时间表，并可与缩略图对账。
 *
 * 用法：node scripts/shot-timeline.mjs
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const file = path.join(__dirname, "..", "src", "SPHExplainer.tsx");
const source = fs.readFileSync(file, "utf8");

// 按 SHOTS 数组内的顺序抓取每个镜头的 title / duration / fullBleed
const blocks = source.split(/\n\s*\{\n/).slice(1);
const rows = [];
let acc = 0;
for (const block of blocks) {
  const durMatch = block.match(/duration:\s*(\d+)/);
  if (!durMatch) continue;
  const titleMatch = block.match(/title:\s*"([^"]*)"/);
  const compMatch = block.match(/Component:\s*(\w+)/);
  const dur = Number(durMatch[1]);
  rows.push({
    start: acc,
    duration: dur,
    title: titleMatch?.[1] || "（片头 / 结尾）",
    component: compMatch?.[1] ?? "?",
    fullBleed: /fullBleed:\s*true/.test(block),
  });
  acc += dur;
}

for (const r of rows) {
  const mark = r.fullBleed ? "[3D] " : "     ";
  console.log(
    `${String(r.start).padStart(5)}  ${String(r.duration).padStart(4)}帧  ${mark}${r.title}  (${r.component})`,
  );
}
console.log(`\n镜头数 ${rows.length}，总帧 ${acc}，时长 ${(acc / 30).toFixed(1)} 秒`);
console.log(`其中三维镜头 ${rows.filter((r) => r.fullBleed).length} 个`);
