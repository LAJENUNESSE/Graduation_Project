"""Generate formula-based figures that do not require benchmark data."""

from __future__ import annotations

import argparse
import os
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[1]
os.environ.setdefault("MPLCONFIGDIR", str(PROJECT_DIR / ".matplotlib-cache"))

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
from matplotlib import font_manager
from matplotlib.axes import Axes
from matplotlib.figure import Figure


SERIES_BLUE = "#0072B2"
SERIES_ORANGE = "#D55E00"
SERIES_GREEN = "#009E73"
GRID_COLOR = "#D9D9D9"
TEXT_COLOR = "#222222"


def configure_matplotlib() -> None:
    """Configure a publication-oriented style with a Chinese font fallback."""
    font_candidates = (
        Path("C:/Windows/Fonts/msyh.ttc"),
        Path("C:/Windows/Fonts/simhei.ttf"),
    )
    font_family = "DejaVu Sans"
    for font_path in font_candidates:
        if font_path.exists():
            font_manager.fontManager.addfont(font_path)
            font_family = font_manager.FontProperties(fname=font_path).get_name()
            break

    plt.rcParams.update(
        {
            "font.family": "sans-serif",
            "font.sans-serif": [font_family, "DejaVu Sans"],
            "axes.unicode_minus": False,
            "axes.edgecolor": TEXT_COLOR,
            "axes.labelcolor": TEXT_COLOR,
            "axes.linewidth": 0.8,
            "axes.titlesize": 10,
            "axes.labelsize": 9,
            "xtick.color": TEXT_COLOR,
            "ytick.color": TEXT_COLOR,
            "xtick.labelsize": 8,
            "ytick.labelsize": 8,
            "legend.fontsize": 8,
            "lines.linewidth": 1.8,
            "grid.color": GRID_COLOR,
            "grid.linewidth": 0.6,
            "grid.alpha": 0.75,
            "figure.dpi": 150,
            "savefig.dpi": 450,
            "savefig.bbox": "tight",
            "savefig.pad_inches": 0.04,
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
        }
    )


def style_axes(axis: Axes) -> None:
    axis.spines["top"].set_visible(False)
    axis.spines["right"].set_visible(False)
    axis.grid(axis="y")
    axis.set_axisbelow(True)


def save_figure(figure: Figure, output_dir: Path, stem: str) -> list[Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    paths = [output_dir / f"{stem}.png", output_dir / f"{stem}.pdf"]
    for path in paths:
        figure.savefig(path, facecolor="white")
    plt.close(figure)
    return paths


def plot_sph_kernel_functions(output_dir: Path) -> list[Path]:
    """Plot normalized radial profiles for the three SPH kernels used by the engine."""
    q = np.linspace(0.0, 1.2, 800)
    supported = q <= 1.0

    poly6 = np.where(supported, np.maximum(1.0 - q**2, 0.0) ** 3, 0.0)
    spiky_gradient = np.where(supported, np.maximum(1.0 - q, 0.0) ** 2, 0.0)
    viscosity_laplacian = np.where(supported, np.maximum(1.0 - q, 0.0), 0.0)

    figure, axes = plt.subplots(1, 3, figsize=(7.2, 2.55), sharex=True)
    panels = (
        (poly6, SERIES_BLUE, r"(a) Poly6核 $W_{\mathrm{poly6}}$"),
        (spiky_gradient, SERIES_ORANGE, r"(b) Spiky核梯度幅值 $\|\nabla W_{\mathrm{spiky}}\|$"),
        (viscosity_laplacian, SERIES_GREEN, r"(c) Viscosity核拉普拉斯 $\nabla^2 W_{\mathrm{visc}}$"),
    )

    for axis, (values, color, title) in zip(axes, panels, strict=True):
        axis.plot(q, values, color=color)
        axis.fill_between(q, values, color=color, alpha=0.10)
        axis.axvline(1.0, color="#666666", linestyle="--", linewidth=0.9)
        axis.set_xlim(0.0, 1.2)
        axis.set_ylim(0.0, 1.06)
        axis.set_xticks(np.arange(0.0, 1.21, 0.2))
        axis.set_yticks(np.arange(0.0, 1.01, 0.25))
        axis.set_xlabel(r"归一化距离 $q=r/h$")
        axis.set_title(title, pad=7)
        style_axes(axis)

    axes[0].set_ylabel("归一化核函数值")
    figure.subplots_adjust(left=0.075, right=0.995, bottom=0.20, top=0.86, wspace=0.30)
    return save_figure(figure, output_dir, "sph_kernel_functions")


def plot_neighbor_search_complexity(output_dir: Path) -> list[Path]:
    """Plot theoretical normalized growth for naive and spatial-hash neighbor search."""
    particle_count = np.geomspace(1_000, 50_000, 500)
    normalized_count = particle_count / particle_count[0]
    linear_growth = normalized_count
    quadratic_growth = normalized_count**2

    figure, axis = plt.subplots(figsize=(5.8, 3.45))
    axis.loglog(
        particle_count,
        quadratic_growth,
        color=SERIES_ORANGE,
        label=r"朴素全粒子搜索 $O(N^2)$",
    )
    axis.loglog(
        particle_count,
        linear_growth,
        color=SERIES_BLUE,
        label=r"空间哈希邻域搜索（理想）$O(N)$",
    )
    axis.scatter([50_000, 50_000], [quadratic_growth[-1], linear_growth[-1]],
                 color=[SERIES_ORANGE, SERIES_BLUE], s=22, zorder=3)
    axis.annotate(
        "2500倍",
        xy=(50_000, quadratic_growth[-1]),
        xytext=(-34, -2),
        textcoords="offset points",
        ha="right",
        va="center",
        fontsize=8,
        color=TEXT_COLOR,
    )
    axis.annotate(
        "50倍",
        xy=(50_000, linear_growth[-1]),
        xytext=(-22, 8),
        textcoords="offset points",
        ha="right",
        va="bottom",
        fontsize=8,
        color=TEXT_COLOR,
    )
    axis.set_xlabel(r"粒子数 $N$")
    axis.set_ylabel("相对计算量（以1000粒子为1）")
    axis.set_xlim(1_000, 55_000)
    axis.set_ylim(0.8, 4_000)
    axis.legend(loc="upper left", frameon=False)
    axis.text(
        0.98,
        0.04,
        "理论增长趋势，非实测性能",
        transform=axis.transAxes,
        ha="right",
        va="bottom",
        fontsize=8,
        color="#666666",
    )
    style_axes(axis)
    figure.subplots_adjust(left=0.14, right=0.98, bottom=0.17, top=0.97)
    return save_figure(figure, output_dir, "neighbor_search_complexity")


def parse_args() -> argparse.Namespace:
    default_output = PROJECT_DIR / "generated"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=default_output,
        help=f"Output directory (default: {default_output})",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    configure_matplotlib()

    generated_paths: list[Path] = []
    generated_paths.extend(plot_sph_kernel_functions(args.output_dir))
    generated_paths.extend(plot_neighbor_search_complexity(args.output_dir))

    for path in generated_paths:
        print(path.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
