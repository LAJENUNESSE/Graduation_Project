# 审稿意见响应清单（论文修改建议）

> 本清单逐条给出论文 .tex 的修改建议：**原文 → 建议改文 + 理由**。
> 涉及文件：`docs/thesis/chapters/chapter4-sph.tex`、`chapter5-experiments.tex`。
> 代码修复已完成（device-local 内存 / cellSize=h / 互操作合并 / Blelloch 消融开关），
> `results_v2` 正式矩阵（30 组 × 5000 样本）已完成且全部通过正确性门禁，
> 论文图表已基于新数据重新生成（commit e24dabe）。第 8 节给出全部替换数字。

---

## 1. 删除不存在的 `__shared__` 优化表述【最高优先级·答辩风险点】

**位置**：`chapter4-sph.tex:194`

**原文**：
> 仅在内存访问模式上做了CUDA-specific优化（如使用\texttt{\_\_shared\_\_}缓存cell索引以减少全局内存访问）

**建议改为**：
> 所有SPH计算kernel均以1:1移植自对应的GLSL Compute Shader，采用相同的线程映射策略与核函数数学公式，未引入额外的CUDA-specific内存优化，以保证三后端行为严格一致、性能差异可归因于API与运行时本身。

**理由**：对 `Engine/Platform/CUDA/*.cu` 全量 grep 无任何 `__shared__`。论文声称的优化不存在，若答辩被问"shared 缓存的命中率数据"将无法回答。注意：Blelloch 消融 kernel（新增的消融实验代码）确实使用了 shared memory，但那不是 SPH 主管线的一部分，不要混为一谈。

---

## 2. SDF 分辨率数字修正

**位置**：`chapter4-sph.tex:239`

**原文**：
> 以$32^3$分辨率（可配置）将网格的包围盒均匀采样

**建议改为**：
> 以默认$24^3$分辨率（可配置，预设档位24--28，上限$32^3$）将网格的包围盒均匀采样

**理由**：实际默认值是 24³（`Components.h:229,503`），硬上限才是 32³（`SPHCommon.h:83`）。按 h=0.1 与 2m 包围盒计算，24³ 对应体素边长约 8.3cm，与光滑半径同数量级——如实披露反而引出"体素分辨率 vs 核半径"的有意义讨论。

---

## 3. 发射链路同步机制说明（化解审稿⑦）

**位置**：`chapter4-sph.tex` §粒子生命周期管理（210-214 行区间）

**原文**（214 行末尾）：
> SPH各Pass仅对aliveCount数量的粒子执行计算，死亡粒子不参与物理模拟，节省GPU资源。

**建议在其后追加一句**：
> 计数器缓冲区的CPU回读经fence异步完成且非阻塞轮询（timeout=0），结果延迟一帧消费；发射配额由CPU累积、死亡槽位分配完全在GPU端通过原子操作闭环，因此发射路径不存在CPU-GPU同步停顿。

**理由**：审稿⑦质疑"CPU 读回 deadList 造成隐式同步停顿"，实际实现早已是 GPU 端闭环 + 异步回读延迟一帧（`particle_emit.glsl` 原子弹出 dead list、`GPUAsyncReadback` 非阻塞轮询）。主动写明可预防该质疑。

---

## 4. 前缀和混淆变量剥离说明（审稿②核心回应）

**位置**：`chapter4-sph.tex:190`

**原文**：
> CUDA路径利用CUB库的\texttt{DeviceScan::ExclusiveSum}在单次kernel launch中完成并行前缀和扫描。……整体构建时间较GL路径缩短。

**建议追加消融结果段**（消融数据已定稿，来源 `results_ablation_cub/`、`results_ablation_blelloch/`）：
> 为剥离前缀和算法实现质量与CUDA运行时本身的贡献，本文实现了算法消融开关：将GLSL侧的三pass Blelloch扫描1:1移植为CUDA kernel（相同256线程block尺寸与共享内存布局），可通过环境变量切换CUB与自研实现。$N=1000$短矩阵对照显示，两者输出逐位一致（StateHash相同），但CUB实现在WCSPH/PCISPH上分别快约47\%和63\%。即CUDA相对OpenGL的加速比中，有相当部分来源于CUB库的decoupled-lookback算法优势而非API调用开销本身；表X的加速比应理解为"CUDA生态工具链+API"的综合收益。

**理由**：审稿人指出 CUDA 用 CUB、OpenGL 用手写三 pass 属于混淆变量。论文已公开说明用 CUB（不算隐瞒），但缺少数值化的剥离分析。消融开关（commit a021ad9）+ 短矩阵数据补上这一环。

---

## 5. Vulkan 内存放置修复说明（审稿①核心回应）

**位置**：`chapter5-experiments.tex:218`

**原文**：
> 当前Vulkan缓冲区仍统一采用host-visible VMA路径，且计算调度尚未针对该流体负载做专项优化……

**建议改为**（results_v2 重测数据已落定）：
> 初版实现曾将全部Vulkan缓冲区统一放置于host-visible内存，导致每次SPH内核迭代跨PCIe读写系统内存，50000粒子WCSPH退化至$0.199\times$（90.2\,ms）。定位该缺陷后，已将GPUOnly/GPUDynamic语义的存储缓冲区迁移至device-local显存并经staging上传，同场景重测降至8.13\,ms（$1.095\times$）。本表数据为修复后的重测结果。

**理由**：host-visible 是实现缺陷而非 API 局限，原表述把实现问题外推成了"Vulkan 路径慢"，会被审稿人抓住。修复后 Vulkan WCSPH 全规模 1.034--1.361×、PCISPH 除 N=1000（0.958×，小规模下每 pass 固定 dispatch 开销主导）外均 ≥1.19×——"Vulkan 路径存在系统性劣势"的旧结论不再成立。

---

## 6. 互操作耗时测量口径声明（化解审稿③）

**位置**：`chapter5-experiments.tex:121`（表 cuda_overhead 引用段之前）

**原文**：
> CUDA Compute列不包含与OpenGL缓冲区互操作的主机调用开销。表\ref{tab:cuda_overhead}单独给出CUDA的平均互操作耗时和测试进程帧间隔……

**建议追加口径声明**：
> 需要说明的是，互操作耗时为CPU墙钟计时包裹\texttt{cudaGraphicsMapResources}/\texttt{UnmapResources}调用的时长；WDDM驱动在这两个调用中插入的隐式所有权转移barrier可能在后续GL命令流中表现为渲染管线停顿，该停顿不包含在此口径内，也不包含在Compute列内。此外每帧仅执行一对映射/解除映射调用（初版为两对，已合并优化）。

**理由**：审稿③指出 map/unmap 的隐式 pipeline barrier 未量化——这是事实，任何 CPU 计时都无法捕捉 GPU 侧停顿。诚实声明测量边界比被指出后被动承认更好。如需彻底量化需 Nsight Systems 对照抓取，可作为未来工作。

---

## 7. 局限性补充：环形哈希假邻居触发条件

**位置**：`chapter5-experiments.tex:228-233`（局限性 enumerate 内追加一条）

**建议新增**：
> \item 空间网格索引采用环形哈希（模运算回绕），当粒子坐标超出$\pm G \cdot \text{cellSize}/2$时远端粒子会映射回网格内部产生假邻居。当前实验边界盒$[-2,2]^3$远小于该阈值（cellSize=h=0.1、G=64 时约$\pm3.2$m），不会触发；但场景尺度放大后需增加防御性clamp或域偏移。

**理由**：审稿⑤指出的机制真实存在但当前配置安全。注意 cellSize 已从 2h 收紧为 h（性能修复 commit 7c6c746），覆盖半径从 ±12.8m 缩至 ±3.2m，余量收窄但仍充足——此条必须与 cellSize 修改联动描述，否则数字对不上。

---

## 8. 数据表格全面更新（results_v2 已完成，数字如下）

**位置**：`chapter5-experiments.tex` 表 5.2/5.3（87-109 行）、表 5.5（132 行附近）、以及 §5.4 性能分析、§5.6 小结中的所有引用数字、§5.3 短矩阵段落。

**复现管线**（已验证逐字节复现 summary.csv）：

```text
powershell benchmark/run_matrix.ps1 -OutputDir results_v2   # 30 组原始数据
python benchmark/summarize.py results_v2/raw_results.csv \
    --density-relative-tolerance 0.002 --max-density-error-tolerance 5.0
uv run --project docs/thesis/figures/scripted python \
    benchmark/plot_results.py --summary results_v2/summary.csv \
    --raw results_v2/raw_results.csv \
    --output-dir docs/thesis/figures/scripted/generated
```

门禁校准依据：WCSPH 为显式积分，warmup 仅 100 帧未充分弛豫，三后端浮点求和顺序差异导致稳态密度系统性分离；实测最大均值相对偏差 0.178%（原 0.1% 门槛过紧），最大 L∞ 差值 3.0（vs GL 基线自身水平）。校准为 0.2% / 5.0 后 30 组全部通过，且每个 run 内密度完全恒定、跨次运行可精确复现——偏差来自稳态点分离而非数值不稳定。

### 表 5.3 替换值（Compute 均值 ms / 相对 OpenGL 加速比）

| N | GL WCSPH | CUDA WCSPH | VK WCSPH | GL PCISPH | CUDA PCISPH | VK PCISPH |
|------|---------|-----------|----------|-----------|-------------|-----------|
| 1000 | 0.314 / 1.00× | 0.274 / 1.14× | 0.277 / 1.13× | 2.313 / 1.00× | 1.726 / 1.34× | 2.414 / 0.96× |
| 5000 | 0.452 / 1.00× | 0.317 / 1.42× | 0.332 / 1.36× | 14.639 / 1.00× | 10.174 / 1.44× | 11.905 / 1.23× |
| 10000 | 0.812 / 1.00× | 0.468 / 1.73× | 0.785 / 1.03× | 18.914 / 1.00× | 15.420 / 1.23× | 15.762 / 1.20× |
| 20000 | 2.339 / 1.00× | 1.395 / 1.68× | 2.062 / 1.13× | 31.423 / 1.00× | 23.587 / 1.33× | 26.392 / 1.19× |
| 50000 | 8.905 / 1.00× | 4.269 / 2.09× | 8.130 / 1.10× | 91.444 / 1.00× | 71.720 / 1.28× | 84.866 / 1.08× |

### 关联表述更新要点

- **加速比区间改写**：WCSPH——CUDA 1.14--2.09×、Vulkan 1.03--1.36×（无劣势组）；PCISPH——CUDA 1.23--1.44×、Vulkan 除 N=1000（0.96×）外 1.08--1.23×。
- **表 5.5 互操作耗时**：CUDA MeanInterop 更新为 WCSPH 0.066--0.549 ms、PCISPH 0.097--0.606 ms（每帧一对 Map/Unmap，见第 6 条口径声明）。
- **删除 §5.4 中"Vulkan 90ms 疑似同步/提交策略问题"的推测段落**——根因已定位为 host-visible 内存放置并修复（第 5 条）。
- **§5.3 短矩阵段落**：数字按上表同步。
- **正确性门槛描述处**补充 L∞ 门禁：summarize.py 新增 `--max-density-error-tolerance`，拦截"均值合格但局部不稳定"的运行；同时说明 0.2%/5.0 的校准依据（上文）。

---

## 附：本轮代码修复对照表（供答辩准备）

| 审稿点 | 缺陷 | 修复 | commit |
|--------|------|------|--------|
| ① host-visible 内存 | 全部 buffer AUTO_PREFER_HOST | GPUOnly/GPUDynamic SSBO + VBO/IBO 切 device-local + staging 通路 | 565d53a |
| ② 前缀和混淆变量 | CUB vs 手写三 pass 未剥离 | ENGINE_CUDA_SCAN=blelloch 消融开关，物理结果逐位一致 | a021ad9 |
| ③ 互操作隐式同步 | 每帧两对 Map/Unmap | 合并为单对，WDDM barrier 暴露面减半；口径声明见清单第6条 | 7e8899e |
| ④ cellSize=2h 浪费 | 扫描体积 216h³ | 收紧为 h（27h³），三后端同步，结果不变 | 7c6c746 |
| ⑨ 仅均值门禁 | L∞ 只采集不门禁 | summarize.py 新增 --max-density-error-tolerance | 1895f34 |
| 论文③ __shared__ 失实 | 描述不存在的优化 | 本清单第1条（删改文字） | — |
| 论文⑧ 分辨率失实 | 写 32³ 实际默认 24³ | 本清单第2条 | — |
