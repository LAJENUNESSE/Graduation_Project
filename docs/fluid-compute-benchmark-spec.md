# 流体 Compute 三后端 Benchmark 规范

## 1. 目标与范围

本基准用于比较同一 SPH 流体计算任务在 OpenGL Compute、CUDA Sidecar 与 Vulkan Compute 三种后端下的性能与数值一致性，为毕业论文第五章提供可复现的原始数据。

论文主实验仍为 OpenGL Compute 与 CUDA Sidecar 双路径对比；Vulkan Compute 作为扩展后端实验。基准不要求补齐 Vulkan PBR、IBL、后处理、编辑器完整渲染或安装发布流程。

## 2. 公平性约束

- 三种后端必须使用同一份确定性初始粒子数据，禁止依赖墙钟时间生成随机状态。
- 每组实验必须固定随机种子、时间步长、求解器参数与粒子排列。
- 每个后端在独立进程中运行，禁止将逐帧 CUDA/OpenGL 自动交替结果作为论文数据。
- 正式计时阶段关闭粒子发射、动态生命周期、流体渲染、Mesh SDF 与非必要回读。
- GPU Compute 时间仅覆盖空间网格、密度、力与积分阶段；初始化、最终正确性回读和 CSV 写入不计入。
- CUDA 总 Compute 时间必须包含 CUB Prefix Sum；CUDA-OpenGL Map/Unmap 互操作时间另列。
- 后端发生初始化失败、中毒或自动回退时，本组实验必须失败，禁止以回退后端的数据冒充目标后端。

## 3. 固定实验矩阵

| 维度 | 取值 |
| --- | --- |
| 后端 | OpenGL Compute、CUDA、Vulkan Compute |
| 求解器 | WCSPH、PCISPH |
| 粒子数 | 1000、5000、10000、20000、50000 |
| PCISPH 迭代次数 | 6（主实验） |
| 时间步长 | 1/120 s（主实验） |
| 预热帧 | 100 |
| 采样帧 | 1000 |
| 独立重复 | 5 次 |
| 随机种子 | 42（主实验） |

静止密度、气体常数、光滑核半径、粒子质量与边界范围从最终实验场景冻结后写入基准配置和论文，不允许在不同后端间单独调优。

## 4. 命令行接口

目标调用形式：

```powershell
Editor.exe --benchmark-fluid `
  --backend opengl `
  --solver pcisph `
  --particles 10000 `
  --iterations 6 `
  --warmup 100 `
  --frames 1000 `
  --runs 5 `
  --fixed-dt 0.008333333 `
  --seed 42 `
  --output benchmark/results.csv
```

所有参数必须经过范围校验。无效参数、目标后端未编译、CUDA设备不匹配、Vulkan时间戳不受支持或输出文件无法创建时，进程应返回非零退出码并输出明确错误。

## 5. 原始数据格式

原始 CSV 每行表示一个采样帧：

```text
Timestamp,Backend,Solver,Particles,Iterations,Run,Frame,Warmup,
SampleValid,Compute_ms,Interop_ms,EndToEnd_ms,AliveCount,
MeanDensity,MaxDensityError,StateHash,Device
```

汇总 CSV 每行表示一组实验：

```text
Backend,Solver,Particles,Samples,Mean_ms,StdDev_ms,
P50_ms,P95_ms,Min_ms,Max_ms,Speedup
```

原始数据必须保留，不允许只保存汇总表。

## 6. 正确性门槛

每次独立运行结束后，在计时区间外回读状态并检查：

- Alive Count 与预期粒子数一致；
- 位置、速度、密度和压力不存在 NaN 或 Inf；
- 记录平均密度、最大密度误差和均方密度误差；
- 记录越界粒子数量与粒子状态摘要 Hash；
- 不要求不同后端逐位一致，但误差超过冻结的数值容差时，该组不得计算加速比。

正式容差应在三个后端完成第一轮短测试后，基于浮点误差分布确定并记录，不得为隐藏明显算法偏差而事后放宽。

## 7. 统计规则

- 丢弃全部预热帧。
- 保留正常的慢帧，不按主观阈值删除离群点。
- 仅剔除查询未就绪、后端回退、设备错误或明确标记为无效的样本。
- 报告均值、标准差、中位数、P95、最小值与最大值。
- 加速比定义为基线平均 Compute 时间除以目标后端平均 Compute 时间。
- 论文中同时报告 GPU Compute 时间与端到端时间，禁止用不含 Swap/Present 的引擎帧时间代替真实显示帧时间。

## 8. 验收条件

- OpenGL、CUDA、Vulkan 三种后端均能完成短矩阵测试并自动退出。
- 相同配置重复运行可生成结构一致、可追溯的 CSV。
- 后端标签、设备信息和错误状态准确写入结果。
- 三后端通过正确性门槛后才生成性能汇总与加速比。
- 普通 OpenGL 编辑器启动与现有场景行为不受 Benchmark 模式影响。
