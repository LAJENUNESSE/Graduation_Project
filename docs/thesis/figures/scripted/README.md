# 论文脚本绘图

本目录保存可复现的论文绘图脚本和生成结果。

## 目录结构

- `scripts/`：绘图脚本
- `generated/`：脚本生成的 PNG 和 PDF
- `pyproject.toml` / `uv.lock`：由 uv 管理的 Python 环境

## 生成公式图与方法图

```powershell
cd docs/thesis/figures/scripted
uv sync
uv run python scripts/generate_formula_figures.py
uv run python scripts/generate_method_figures.py
```

当前生成：

1. `sph_kernel_functions`：Poly6、Spiky 梯度幅值和 Viscosity 拉普拉斯的归一化径向曲线。
2. `neighbor_search_complexity`：朴素全粒子搜索与空间哈希邻域搜索的理论增长趋势。
3. `sph_interpolation_neighborhood`：紧支撑邻域和粒子核函数贡献。
4. `spatial_hash_27_neighborhood`：目标网格单元及其 $3\times3\times3$ 邻域。
5. `pressure_equation_of_state`：不同气体常数下的压力状态方程。
6. `sdf_collision_projection`：Sphere/Box SDF和穿透粒子硬投影。
7. `mesh_sdf_trilinear_sampling`：体素SDF的八角点三线性插值。
8. `wcsph_pcisph_compute_pipelines`：WCSPH与PCISPH计算Pass对比。
9. `particle_lifecycle_recycling`：粒子发射、失效、紧凑与槽位复用流程。

其中复杂度图是理论参考图，不是实验结果；论文图题和正文中不得将其描述为实测性能。

PNG 用于预览和 PPT，PDF 为矢量格式，优先用于 LaTeX 论文。

## 生成正式实验图

正式实验图读取 `benchmark/results/summary.csv` 与 `raw_results.csv`：

```powershell
cd ../../../../
uv run --project docs/thesis/figures/scripted python benchmark/plot_results.py `
  --summary benchmark/results/summary.csv `
  --raw benchmark/results/raw_results.csv `
  --output-dir docs/thesis/figures/scripted/generated
```

当前生成GPU计算耗时、加速比、平均密度与相对偏差一致性、双对数规模扩展性、
CUDA端到端耗时构成、实时帧预算和逐帧耗时分布七组正式实验图。规模扩展图中的
$O(N)$线仅表示锚定于OpenGL 10000粒子结果的参考斜率，不是额外实验数据。绘图
脚本拒绝处理未通过正确性门槛的实验组。

