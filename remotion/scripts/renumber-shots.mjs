/**
 * 按出现顺序重排镜头编号。
 *
 * 手工插入镜头后编号容易重复或跳号；这里以文件中的出现顺序为准
 * 重新编号，跳过片头与结尾（index 为空字符串）。
 *
 * 用法：node scripts/renumber-shots.mjs
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const file = path.join(__dirname, "..", "src", "SPHExplainer.tsx");

const source = fs.readFileSync(file, "utf8");
const lines = source.split(/\r?\n/);

let counter = 0;
let changed = 0;
const out = lines.map((line) => {
  const m = line.match(/^(\s*index: ")(\d*)(".*)$/);
  if (!m) return line;
  if (m[2] === "") return line; // 片头/结尾保持空
  counter += 1;
  const next = String(counter).padStart(2, "0");
  if (next !== m[2]) changed += 1;
  return `${m[1]}${next}${m[3]}`;
});

fs.writeFileSync(file, out.join("\n"), "utf8");
console.log(`重编号完成，共 ${counter} 个镜头，改动 ${changed} 处。`);
