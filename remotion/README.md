# Remotion 数据可视化（论文插图动画）

用 React 代码生成论文插图动画与数据可视化视频。与 `docs/thesis/figures/scripted/`
的 matplotlib 静态插图互补：静态图仍由 Python 脚本产出，需要运动或演示时才用这里。

## 环境

- Node.js ≥ 20.19（当前使用 v24）
- 依赖安装在 `remotion/node_modules/`，不入库

```powershell
cd remotion
npm install
```

## 常用命令

```powershell
# 打开 Studio 预览（浏览器里逐帧调参）
npm run dev

# 导出整段视频
npx remotion render BarChart out/bar-chart.mp4

# 只导出静帧，适合截图进论文
npx remotion still BarChart out/frame.png --frame=45

# 类型检查 + ESLint
npm run lint
```

## 目录结构

| 路径 | 作用 |
| --- | --- |
| `src/index.ts` | Remotion 入口，注册根组件 |
| `src/Root.tsx` | 所有 Composition 的注册处，声明尺寸、帧率、时长 |
| `src/data.ts` | 数据源与其类型定义，示例数据在这里替换 |
| `src/BarChart.tsx` | 带入场动画的分组柱状图组件 |
| `out/` | 渲染产物，已 gitignore |

## 换成真实实验数据

1. 打开 `src/data.ts`，把 `SAMPLE_DATASET` 的数值替换成 `benchmark/results` 或
   `results_v3/` 里的实测结果。
2. 保持 `series[].values` 的长度与 `labels` 一致，`color` 建议沿用论文配色的十六进制值。
3. 若要新增图表类型，参照 `src/BarChart.tsx` 的写法：动画只依赖 `useCurrentFrame()`，
   不要用 `Math.random()` 或 `Date.now()`，否则每次渲染结果都会不一致。
4. 在 `src/Root.tsx` 里注册新的 `<Composition>`，Studio 会自动出现对应条目。

## 注意

- 本机启动 Studio 或渲染时子进程需要管道 stdio；若在受限沙箱里运行会报
  `spawn EPERM`，需要在正常权限下执行。
- 渲染视频依赖 Remotion 首次自动下载的 Chrome Headless Shell，缓存在用户目录。
