# Fluid Compute Benchmark

## 一键流水线（推荐入口）

`run_pipeline.ps1` 把「构建基准 Editor → 跑矩阵 → 正确性门禁汇总 → 生成论文图表」串成一条命令。改完代码后直接挂上即可，无需手动逐步执行：

```powershell
# 冒烟验证：1000 粒子 × 20 帧 × 1 次，验证端到端管线可用（约几分钟）
./benchmark/run_pipeline.ps1 -Mode quick

# 正式全量矩阵：30 组规范实验（6 迭代 / 100 warmup / 1000 帧 / 5 次），耗时数小时
./benchmark/run_pipeline.ps1

# 前缀和消融实验：CUDA 改用与 GL/Vulkan 同源的 Blelloch 三 pass 扫描
./benchmark/run_pipeline.ps1 -CudaScan blelloch -Name ablation_blelloch

# 中断后续跑（复用配置匹配且样本完整的分组）；论文定稿数据可放仓库根目录留存
./benchmark/run_pipeline.ps1 -Resume -OutputRoot results_v3
```

行为要点：

* 构建走 `vs2022-benchmark` 预设（独立目录 `build-benchmark/`，OpenGL+Vulkan+CUDA 三后端，不影响日常 `build/`）。
* 结果默认写入 `benchmark/results/run_<时间戳>/`（已被 .gitignore 排除）；`-Name` 自定义子目录名。
* 正确性门禁：均值密度相对偏差 ≤ 0.2%（`-DensityRelativeTolerance`）且逐样本 L∞ 偏差与 OpenGL 基线绝对差 ≤ 5.0（`-MaxDensityErrorTolerance`）；任一分组超标以退出码 3 失败并列出明细。
* quick 模式 warmup 仅 5 帧、WCSPH 密度未收敛，门禁自动跳过（只验证管线连通性，不判物理对错）；逐帧分布图需要完整五规模矩阵数据，quick 模式同样自动跳过出图。
* `-DryRun` 只打印将执行的命令；退出码含义见脚本头部注释。

## 分步流程（流水线的内部步骤，也可手动执行）

短测试只检查三后端能否运行、自动退出并生成结构一致的 CSV：

先使用独立实验目录配置并构建三后端版本，不会覆盖正在使用的普通 `build/`：

```powershell
& "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset vs2022-benchmark
& "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build-benchmark --config RelWithDebInfo --target Editor
```

```powershell
./benchmark/run_matrix.ps1 -Quick
```

正式矩阵使用规范中的默认参数：

```powershell
./benchmark/run_matrix.ps1
```

正式矩阵耗时较长，可使用 `-Resume` 复用配置完全匹配且样本完整的分组结果；不完整、参数不匹配或含无效样本的分组会自动重跑：

```powershell
./benchmark/run_matrix.ps1 -Resume
```

运行器会为每个后端、求解器和粒子数启动独立的 `Editor.exe` 进程，并将分组文件合并为
`benchmark/results/raw_results.csv`。结果目录是本地实验产物，不提交到 Git。

先生成不含加速比的统计表：

```powershell
python benchmark/summarize.py benchmark/results/raw_results.csv
```

三后端短矩阵校准测得平均密度最大相对偏差为 0.0261%，最初冻结 0.1%（0.001）容差；cellSize 从 2h 收紧为 h 后 WCSPH 稳态点出现 ±1.5 密度单位的跨后端系统性分离（浮点求和顺序差异，非实现错误，详见 results_v2 逐帧分析），正式门槛随之重校准为均值 0.2% + L∞ 绝对差 5.0（与 `run_pipeline.ps1` 默认值一致）。显式传入容差生成受正确性门槛约束的加速比：

```powershell
python benchmark/summarize.py benchmark/results/raw_results.csv --density-relative-tolerance 0.002 --max-density-error-tolerance 5.0
```

注意：summarize.py 门禁失败时退出码仍为 0（只在 CSV 标 `CorrectnessValid=False`），脚本化使用时需自行解析 summary.csv 判败——`run_pipeline.ps1` 已内置该逻辑。

在没有冻结容差时，汇总脚本会保留 `Speedup` 为空，避免把数值偏差明显的后端结果写成论文加速比。

生成论文用的矢量图和高分辨率预览图。绘图依赖由论文图表目录中的 uv
项目统一管理：

```powershell
uv run --project docs/thesis/figures/scripted python benchmark/plot_results.py `
  --summary benchmark/results/summary.csv `
  --raw benchmark/results/raw_results.csv `
  --output-dir docs/thesis/figures/scripted/generated
```

绘图脚本只接受通过正确性门槛的实验组，输出GPU Compute耗时、相对OpenGL
加速比、跨后端平均密度与相对偏差、双对数规模扩展性、CUDA端到端耗时构成、
实时帧预算和逐帧耗时分布图。每张图同时生成PDF矢量版和PNG高分辨率预览版。
