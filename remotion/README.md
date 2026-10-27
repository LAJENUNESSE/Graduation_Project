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

# 渲染 SPH 原理讲解片（约 4 分 25 秒，1920×1080，全片约 25~40 分钟）
npx remotion render SPHExplainer out/SPH-Principles.mp4

# 只渲染某一段（例如第 2 个镜头的前 200 帧）
npx remotion render SPHExplainer out/part.mp4 --frames=150-550

# 导出单帧静帧，适合截图放进 PPT
npx remotion still SPHExplainer out/frame.png --frame=1200

# 类型检查 + ESLint
npm run lint
```

渲染很慢是正常的：镜头 08/09 的液面用片元着色器逐像素做光线步进，
又没有 GPU 加速。`remotion.config.ts` 已把超时放宽到 4 分钟，
若改用 Node API 渲染需自行传 `timeoutInMilliseconds`。

## SPH 原理片的结构

全片 17 个编号镜头，时间表与字幕都集中在 `src/SPHExplainer.tsx` 的 `SHOTS` 数组里，
调整节奏只需要改那一处。

其中 **5 个镜头是真正的三维渲染**（Three.js），因为它们讲的内容本身就是空间结构，
平面示意图会讲错或严重简化：

| 镜头 | 内容 | 为什么必须 3D |
| --- | --- | --- |
| 03 | 支撑域是一个球 | 支撑域是球而非圆，球外权重恒为 0 |
| 07 | 27 格空间哈希邻域 | 平面只能画 3×3 九格，真实是 27 格 |
| 08 | PCISPH 预测-校正 | 压缩、堆积、侧向挤出都是空间行为 |
| 09 | 从粒子到液面 | 真实渲染里粒子不可见，只有连续液面可见 |

其中 08 / 09 两个镜头**不是把粒子画成小球**，而是把粒子位置喂给密度场，
由片元着色器做光线步进求出等值面，再按厚度做吸收、按视角做 Fresnel。
这对应项目里 SSFR 的思路，也避免「一堆漂浮小球」的玩具感。

| 镜头 | 内容 | 主要源码锚点 |
| --- | --- | --- |
| 片头 | 标题与全片定位 | — |
| 01 | 拉格朗日离散：为什么不划网格 | `ParticleSystemGPU` |
| 02 | SPH 母式：核函数近似 | `SPHKernelMath.h` |
| 03 | **支撑域是一个球（3D）** | `SmoothingRadius = h` |
| 04 | Poly6 与 Spiky 的分工 | `SPHKernelMath::Compute()` |
| 05 | 密度估计与状态方程 | `sph_density.glsl` |
| 06 | WCSPH 力模型（压力/粘性/表面张力/边界） | `sph_force.glsl` |
| 07 | **27 格空间哈希邻域（3D）** | `SpatialHashGrid` |
| 08 | **PCISPH 预测-校正迭代（3D 液面）** | `sph_pcisph_*.glsl` |
| 09 | **从粒子到液面（3D 液面）** | 对应 SSFR 管线 |
| 10 | δ 推导与网格重建取舍 | `ComputePCISPHDelta()` |
| 11 | 工程稳定化措施 | `SPHCommon.cpp` |
| 12 | 参数的三层结构 | `Components.h` |
| 13 | GPU 数据布局 | `ParticleSystemGPU.cpp` |
| 14 | CUDA / OpenGL 双后端一致性 | `CudaSPHPipeline.h` |
| 15 | 实测性能（真实 benchmark 数据） | `benchmark/results` |
| 16 | WCSPH 与 PCISPH 的取舍 | — |
| 17 | 常见失败模式与排查 | 调参手册 |
| 16 | 结尾：完整链路总结 | — |

## 目录结构

| 路径 | 作用 |
| --- | --- |
| `src/index.ts` | Remotion 入口，注册根组件 |
| `src/Root.tsx` | Composition 注册处，声明尺寸、帧率、时长 |
| `src/theme.ts` | 配色与字体常量，视频与论文配图保持一致 |
| `src/SPHExplainer.tsx` | 全片时间线与字幕 |
| `src/components/` | 版式骨架、字幕条、手写数学排版、通用动画 |
| `src/scenes/` | 各镜头场景组件（2D 版式） |
| `src/scenes/three/` | 三维镜头：Three.js 场景、支撑球、27 格网格、液面渲染与流体模拟数据 |
| `scripts/` | 出片、重编号与自动体检脚本 |
| `out/` | 渲染产物，已 gitignore |

## 三维镜头怎么写

3D 镜头走 `src/scenes/three/Scene3D.tsx` 的 `Scene3DShot` 全幅版式：
ThreeCanvas 铺满 1920×1080，标题、HUD 与字幕作为叠加层浮在上面，
视觉语言与 2D 镜头一致，交替出现时不会像两支片子。

需要自己控制相机与着色器材质的镜头（如液面渲染），用 `ShotChrome`
只取叠加层，画布自己搭。

在 `SHOTS` 里给镜头加 `fullBleed: true`，组件就会收到
`index / title / anchor / captions / withChrome`，自行渲染叠加层。

关键约束：

- **模拟必须整段预积分**。Remotion 会并发、乱序渲染各帧，
  任何「依赖上一帧状态」的写法都会错乱。参见 `fluidData.ts` 里
  `simulateDamBreak()` 一次性算完整段、再按帧取快照的做法。
- **流体不要画成一堆小球**。真实渲染里粒子不可见，只有液面可见。
  见下面「液面渲染」一节。
- headless 下走软件 WebGL（Remotion 默认 `hardwareAcceleration: "disable"`），
  能渲染，但成本要自己控制：液面镜头用半分辨率渲染再放大，
  单帧可降到约四分之一。

## 液面渲染（为什么不用 Marching Cubes）

初始版本把粒子画成一个个球，看起来像「玩具粒子堆」，不像流体。
为了让粒子不可见、只让液面可见，有两条路：

**Marching Cubes（已放弃）**
`three/examples/jsm/objects/MarchingCubes.js` 在 Node 侧能正确生成几何
（实测 11040 个真实顶点、包围盒 ±0.5），但在 headless 软件 WebGL 下
始终画不出像素。排查过剔除、材质、创建时机、drawRange，均无效。
这个结论记录在此，避免以后重复踩坑。

**片元着色器光线步进（当前方案）**
`FluidSurface3D.tsx` 把粒子位置写进 `DataTexture`，片元着色器在密度场里
做 sphere tracing 求等值面，再按厚度做 Beer-Lambert 吸收、按视角做
Fresnel 反射。粒子只作为场的数据源，屏幕上永远不会出现小球。
这与项目 SSFR 的思路一致（先重建表面，再着色）。

成本控制要点：
- 每像素遍历全部粒子，所以粒子数控制在约 500（`fluidData.ts`）
- 用水体包围盒做粗剔除，包围盒外可大步跳过
- 半分辨率渲染后用 CSS 放大（`RENDER_SCALE = 0.5`）


## 自动体检脚本

无法靠肉眼逐帧检查时，用这几个脚本代替：

```powershell
# 按镜头统计有效内容占比，找出内容塌陷或空白的画面
node scripts/check-shots.mjs

# 逐张检查 PNG 是否越界、是否整体空画面
node scripts/check-pixels.mjs out/shots

# 验证某帧确实是立体的、有明暗变化的画面（而非全黑或纯色）
node scripts/check-3d.mjs out/probe.png

# 与色调无关的主体占比（适用于任意配色的镜头）
node scripts/check-subject.mjs out/shots/f03370.png

# 液体外观专用：蓝青主色、高光、明暗层次（仅适用于蓝色水）
node scripts/check-water.mjs out/frame.png

# 轮廓光滑度：区分「连续液面」与「一堆小球」
node scripts/check-silhouette.mjs a.png b.png

# 成片卡死检测：连续多秒画面无变化
node scripts/check-motion.mjs out/video.mp4

# 输出镜头时间表（标题 / 时长 / 是否全幅 3D）
node scripts/shot-timeline.mjs

# 插入镜头后按出现顺序重排编号
node scripts/renumber-shots.mjs
```

`check-shots.mjs` 会区分「镜头开头的刻意淡入」和「真正的空白缺陷」，
只报告镜头中段的异常。`check-3d.mjs` 统计非背景像素占比、色彩桶数量与
平均亮度——「渲染没报错」不等于「画面里真有几何体」，这个脚本补上这一步。

注意 `check-water.mjs` 用「蓝色明显高于红色」判定液体，只适用于蓝色水。
镜头 08 故意用密度热力色（偏红），对它跑会误报「几乎没水」；
这类镜头请用 `check-subject.mjs`。

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
