# 论文脚本绘图

本目录保存可复现的论文绘图脚本和生成结果。

## 目录结构

- `scripts/`：绘图脚本
- `generated/`：脚本生成的 PNG 和 PDF
- `pyproject.toml` / `uv.lock`：由 uv 管理的 Python 环境

## 生成公式图

```powershell
cd docs/thesis/figures/scripted
uv sync
uv run python scripts/generate_formula_figures.py
```

当前生成：

1. `sph_kernel_functions`：Poly6、Spiky 梯度幅值和 Viscosity 拉普拉斯的归一化径向曲线。
2. `neighbor_search_complexity`：朴素全粒子搜索与空间哈希邻域搜索的理论增长趋势。

其中复杂度图是理论参考图，不是实验结果；论文图题和正文中不得将其描述为实测性能。

PNG 用于预览和 PPT，PDF 为矢量格式，优先用于 LaTeX 论文。

