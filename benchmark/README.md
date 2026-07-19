# Fluid Compute Benchmark

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

运行器会为每个后端、求解器和粒子数启动独立的 `Editor.exe` 进程，并将分组文件合并为
`benchmark/results/raw_results.csv`。结果目录是本地实验产物，不提交到 Git。

先生成不含加速比的统计表：

```powershell
python benchmark/summarize.py benchmark/results/raw_results.csv
```

完成三后端短测试并冻结论文采用的密度相对误差容差后，再显式传入容差生成加速比。例如容差为 5%：

```powershell
python benchmark/summarize.py benchmark/results/raw_results.csv --density-relative-tolerance 0.05
```

在没有冻结容差时，汇总脚本会保留 `Speedup` 为空，避免把数值偏差明显的后端结果写成论文加速比。
