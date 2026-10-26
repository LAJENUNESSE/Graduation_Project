# Remotion 视频工程

用 React 代码生成视频。当前有两个 Composition：

- **`SPHExplainer`** —— 主片，约 4 分钟，讲解本项目 SPH 流体模拟原理
- **`BarChart`** —— 通用柱状图组件，可用于论文插图动画

## 环境

- Node.js ≥ 20.19（当前使用 v24）
- 依赖安装在 `remotion/node_modules/`，不入库

```powershell
cd remotion
npm install
```

## 常用命令

```powershell
# 打开 Studio 预览（浏览器里逐帧查看）
npm run dev

# 渲染 SPH 原理讲解片（约 4 分钟，1920×1080）
npx remotion render SPHExplainer out/SPH-Principles.mp4

# 只渲染某一段（例如第 2 个镜头的前 200 帧）
npx remotion render SPHExplainer out/part.mp4 --frames=150-550

# 导出单帧静帧，适合截图放进 PPT
npx remotion still SPHExplainer out/frame.png --frame=1200

# 类型检查 + ESLint
npm run lint
```

## SPH 原理片的结构

全片 17 个镜头，时间表与字幕都集中在 `src/SPHExplainer.tsx` 的 `SHOTS` 数组里，
调整节奏只需要改那一处。

| 镜头 | 内容 | 主要源码锚点 |
| --- | --- | --- |
| 片头 | 标题与全片定位 | — |
| 01 | 拉格朗日离散：为什么不划网格 | `ParticleSystemGPU` |
| 02 | SPH 母式：核函数近似 | `SPHKernelMath.h` |
| 03 | Poly6 与 Spiky 的分工 | `SPHKernelMath::Compute()` |
| 04 | 密度估计与状态方程 | `sph_density.glsl` |
| 05 | WCSPH 力模型（压力/粘性/表面张力/边界） | `sph_force.glsl` |
| 06 | 空间哈希邻居搜索 | `SpatialHashGrid` |
| 07 | PCISPH 预测-校正迭代 | `sph_pcisph_*.glsl` |
| 08 | δ 推导与网格重建取舍 | `ComputePCISPHDelta()` |
| 09 | 工程稳定化措施 | `SPHCommon.cpp` |
| 10 | 参数的三层结构 | `Components.h` |
| 11 | GPU 数据布局 | `ParticleSystemGPU.cpp` |
| 12 | CUDA / OpenGL 双后端一致性 | `CudaSPHPipeline.h` |
| 13 | 实测性能（真实 benchmark 数据） | `benchmark/results` |
| 14 | WCSPH 与 PCISPH 的取舍 | — |
| 15 | 常见失败模式与排查 | 调参手册 |
| 16 | 结尾：完整链路总结 | — |

## 目录结构

| 路径 | 作用 |
| --- | --- |
| `src/index.ts` | Remotion 入口，注册根组件 |
| `src/Root.tsx` | Composition 注册处，声明尺寸、帧率、时长 |
| `src/theme.ts` | 配色与字体常量，视频与论文配图保持一致 |
| `src/SPHExplainer.tsx` | 全片时间线与字幕 |
| `src/components/` | 版式骨架、字幕条、手写数学排版、通用动画 |
| `src/scenes/` | 各镜头场景组件 |
| `scripts/` | 出片与自动体检脚本 |
| `out/` | 渲染产物，已 gitignore |

## 两个自动体检脚本

无法靠肉眼逐帧检查时，用这两个脚本代替：

```powershell
# 按镜头统计有效内容占比，找出内容塌陷或空白的画面
node scripts/check-shots.mjs

# 逐张检查 PNG 是否越界、是否整体空画面
node scripts/check-pixels.mjs out/shots
```

`check-shots.mjs` 会区分「镜头开头的刻意淡入」和「真正的空白缺陷」，
只报告镜头中段的异常。

## 写作约定

- **动画必须只依赖 `useCurrentFrame()`**。用 `Math.random()` 或 `Date.now()`
  会导致同一帧每次渲染结果不同，成片里表现为闪烁。需要伪随机时用
  `components/Common.tsx` 里的 `seeded()`。
- 公式用手写的 `components/Math.tsx` 排版，不引入 KaTeX——它会带外部字体，
  headless 渲染时字体没加载好会静默降级成难看的回退字形。
- 新增镜头：在 `src/scenes/` 写组件，再在 `SPHExplainer.tsx` 的 `SHOTS` 里加一条，
  同步更新 `scripts/check-shots.mjs` 里的 `SHOT_TABLE`。

## 注意

- 渲染依赖 Remotion 首次自动下载的 Chrome Headless Shell，缓存在用户目录。
- 本机 `npm install` 若报 `EPERM`，通常是用户级 npm 缓存目录写入被拒，
  可用 `--cache` 指向临时目录绕过；`--ignore-scripts` 也能绕开生命周期脚本。
