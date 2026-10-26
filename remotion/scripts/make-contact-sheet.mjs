/**
 * 生成整片的缩略图拼版，用于人工抽查画面。
 *
 * 当前环境里模型无法直接看图，所以这个脚本的真正用途是：
 * 把每个镜头的中点帧渲染成小图，交给使用者一眼扫过整片节奏，
 * 同时它也是「渲染流程没有中途崩溃」的证据。
 *
 * 用法：node scripts/make-contact-sheet.mjs
 */
import { bundle } from "@remotion/bundler";
import { renderStill, selectComposition, ensureBrowser } from "@remotion/renderer";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(__dirname, "..");
const outDir = path.join(root, "out", "shots");

const main = async () => {
  fs.mkdirSync(outDir, { recursive: true });
  await ensureBrowser();

  const serveUrl = await bundle({
    entryPoint: path.join(root, "src", "index.ts"),
    onProgress: () => undefined,
  });

  const composition = await selectComposition({
    serveUrl,
    id: "SPHExplainer",
  });

  console.log(`总帧数 ${composition.durationInFrames}（${(composition.durationInFrames / composition.fps).toFixed(1)} 秒）`);

  // 每 60 帧取一帧，覆盖全片
  const frames = [];
  for (let f = 40; f < composition.durationInFrames; f += 90) {
    frames.push(f);
  }

  for (const frame of frames) {
    const output = path.join(outDir, `f${String(frame).padStart(5, "0")}.png`);
    await renderStill({
      composition,
      serveUrl,
      output,
      frame,
      scale: 0.25,
    });
  }

  console.log(`已输出 ${frames.length} 张缩略图到 ${outDir}`);
};

main().catch((e) => {
  console.error(e);
  process.exitCode = 1;
});
