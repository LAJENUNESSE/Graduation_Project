"""Generate method figures from formulas and deterministic synthetic geometry."""

from __future__ import annotations

from itertools import product
from pathlib import Path

import numpy as np

from generate_formula_figures import (
    GRID_COLOR,
    PROJECT_DIR,
    SERIES_BLUE,
    SERIES_GREEN,
    SERIES_ORANGE,
    TEXT_COLOR,
    configure_matplotlib,
    save_figure,
    style_axes,
)

import matplotlib.pyplot as plt
from matplotlib import colormaps
from matplotlib.axes import Axes
from matplotlib.patches import Circle, FancyArrowPatch, FancyBboxPatch, Rectangle


SERIES_PURPLE = "#7A3E9D"
SERIES_GRAY = "#777777"
LIGHT_BLUE = "#DCEEF8"
LIGHT_ORANGE = "#FBE6D4"
LIGHT_GREEN = "#DDF3EA"


def plot_sph_interpolation_neighborhood(output_dir: Path) -> list[Path]:
    rng = np.random.default_rng(20260719)
    positions = rng.uniform(-1.3, 1.3, size=(44, 2))
    distances = np.linalg.norm(positions, axis=1)
    inside = distances < 1.0
    weights = np.where(inside, np.maximum(1.0 - distances**2, 0.0) ** 3, 0.0)

    figure, axes = plt.subplots(1, 2, figsize=(7.1, 3.15))
    spatial_axis, contribution_axis = axes

    spatial_axis.scatter(
        positions[~inside, 0],
        positions[~inside, 1],
        s=18,
        color="#B8B8B8",
        marker="o",
        label="邻域外粒子",
    )
    spatial_axis.scatter(
        positions[inside, 0],
        positions[inside, 1],
        s=24 + 82 * weights[inside],
        c=weights[inside],
        cmap="Blues",
        vmin=0.0,
        vmax=1.0,
        edgecolors=SERIES_BLUE,
        linewidths=0.55,
        label="有效邻居",
    )
    spatial_axis.scatter([0], [0], s=92, color=SERIES_ORANGE, marker="*", zorder=5,
                         label="目标粒子 $i$")
    spatial_axis.add_patch(Circle((0, 0), 1.0, fill=False, linestyle="--",
                                  linewidth=1.2, color=SERIES_ORANGE))
    spatial_axis.annotate(
        r"光滑半径 $h$",
        xy=(0.70, 0.70),
        xytext=(0.95, 1.13),
        arrowprops={"arrowstyle": "->", "color": SERIES_ORANGE, "linewidth": 0.9},
        ha="center",
        fontsize=8,
    )
    spatial_axis.set_xlim(-1.42, 1.42)
    spatial_axis.set_ylim(-1.42, 1.42)
    spatial_axis.set_aspect("equal")
    spatial_axis.set_xlabel(r"$x/h$")
    spatial_axis.set_ylabel(r"$y/h$")
    spatial_axis.set_title("(a) 紧支撑邻域与有效粒子")
    spatial_axis.legend(loc="lower left", frameon=False, fontsize=7)
    style_axes(spatial_axis)

    neighbor_distance = distances[inside]
    neighbor_weight = weights[inside]
    order = np.argsort(neighbor_distance)
    neighbor_distance = neighbor_distance[order]
    neighbor_weight = neighbor_weight[order]
    colors = colormaps["Blues"](0.35 + 0.60 * neighbor_weight)
    contribution_axis.bar(
        np.arange(len(neighbor_distance)),
        neighbor_weight,
        color=colors,
        edgecolor=SERIES_BLUE,
        linewidth=0.45,
    )
    contribution_axis.set_xlabel("按距离排序的邻居粒子")
    contribution_axis.set_ylabel(r"归一化权重 $W_{ij}/W(0)$")
    contribution_axis.set_ylim(0.0, 1.05)
    contribution_axis.set_title("(b) 邻居粒子的核函数贡献")
    contribution_axis.text(
        0.98,
        0.94,
        "距离越近，贡献越大",
        transform=contribution_axis.transAxes,
        ha="right",
        va="top",
        fontsize=8,
        color=SERIES_GRAY,
    )
    style_axes(contribution_axis)
    figure.tight_layout(w_pad=1.45)
    return save_figure(figure, output_dir, "sph_interpolation_neighborhood")


def plot_spatial_hash_neighborhood(output_dir: Path) -> list[Path]:
    figure = plt.figure(figsize=(5.9, 4.35))
    axis = figure.add_subplot(111, projection="3d")

    for x, y, z in product(range(3), repeat=3):
        is_center = (x, y, z) == (1, 1, 1)
        axis.bar3d(
            x,
            y,
            z,
            1,
            1,
            1,
            color=SERIES_ORANGE if is_center else SERIES_BLUE,
            alpha=0.64 if is_center else 0.075,
            edgecolor=SERIES_ORANGE if is_center else "#759CB5",
            linewidth=0.75,
            shade=False,
        )

    rng = np.random.default_rng(20260720)
    particles = rng.uniform(0.08, 2.92, size=(30, 3))
    axis.scatter(
        particles[:, 0], particles[:, 1], particles[:, 2],
        s=13, color=SERIES_BLUE, depthshade=False, alpha=0.82,
    )
    axis.scatter([1.5], [1.5], [1.5], s=80, color=SERIES_ORANGE, marker="*",
                 depthshade=False)
    axis.text(1.55, 1.55, 1.72, "目标粒子", color=TEXT_COLOR, fontsize=8)

    axis.set_xlim(0, 3)
    axis.set_ylim(0, 3)
    axis.set_zlim(0, 3)
    axis.set_xticks([0.5, 1.5, 2.5], [r"$c_x-1$", r"$c_x$", r"$c_x+1$"])
    axis.set_yticks([0.5, 1.5, 2.5], [r"$c_y-1$", r"$c_y$", r"$c_y+1$"])
    axis.set_zticks([0.5, 1.5, 2.5], [r"$c_z-1$", r"$c_z$", r"$c_z+1$"])
    axis.set_xlabel("网格 $x$", labelpad=8)
    axis.set_ylabel("网格 $y$", labelpad=8)
    axis.set_zlabel("网格 $z$", labelpad=6)
    axis.set_title(r"目标单元及其 $3\times3\times3$ 邻域", pad=8)
    axis.view_init(elev=25, azim=38)
    axis.set_box_aspect((1, 1, 1))
    axis.grid(False)
    for pane in (axis.xaxis.pane, axis.yaxis.pane, axis.zaxis.pane):
        pane.set_alpha(0.0)
    figure.subplots_adjust(left=0.02, right=0.96, bottom=0.03, top=0.91)
    return save_figure(figure, output_dir, "spatial_hash_27_neighborhood")


def plot_pressure_equation_of_state(output_dir: Path) -> list[Path]:
    rest_density = 1000.0
    density = np.linspace(800.0, 1200.0, 600)
    constants = ((25.0, SERIES_BLUE), (50.0, SERIES_ORANGE), (100.0, SERIES_GREEN))

    figure, axis = plt.subplots(figsize=(5.8, 3.35))
    for gas_constant, color in constants:
        pressure = gas_constant * np.maximum(density - rest_density, 0.0)
        axis.plot(
            density / rest_density,
            pressure,
            color=color,
            label=rf"$k={gas_constant:.0f}$",
        )
    axis.axvline(1.0, color="#666666", linestyle="--", linewidth=0.9)
    axis.axvspan(0.8, 1.0, color="#B8B8B8", alpha=0.10)
    axis.text(0.90, 17500, "负压力截断区", ha="center", fontsize=8, color=SERIES_GRAY)
    axis.annotate(
        r"静止密度 $\rho_0$",
        xy=(1.0, 0.0),
        xytext=(1.025, 4300),
        arrowprops={"arrowstyle": "->", "color": SERIES_GRAY, "linewidth": 0.8},
        fontsize=8,
    )
    axis.set_xlim(0.8, 1.2)
    axis.set_ylim(0, 21000)
    axis.set_xlabel(r"归一化密度 $\rho/\rho_0$")
    axis.set_ylabel(r"压力 $P=k\max(0,\rho-\rho_0)$")
    axis.legend(frameon=False, loc="upper left")
    style_axes(axis)
    figure.tight_layout()
    return save_figure(figure, output_dir, "pressure_equation_of_state")


def box_sdf(x: np.ndarray, y: np.ndarray, half_x: float, half_y: float) -> np.ndarray:
    qx = np.abs(x) - half_x
    qy = np.abs(y) - half_y
    outside = np.hypot(np.maximum(qx, 0.0), np.maximum(qy, 0.0))
    inside = np.minimum(np.maximum(qx, qy), 0.0)
    return outside + inside


def draw_projection(
    axis: Axes,
    point: np.ndarray,
    projected: np.ndarray,
    label_offset: tuple[float, float],
    projected_label_offset: tuple[float, float] = (0.03, 0.03),
) -> None:
    axis.scatter(*point, s=38, color=SERIES_ORANGE, zorder=5)
    axis.scatter(*projected, s=30, facecolor="white", edgecolor=SERIES_GREEN,
                 linewidth=1.2, zorder=5)
    axis.add_patch(
        FancyArrowPatch(
            point,
            projected,
            arrowstyle="-|>",
            mutation_scale=11,
            linewidth=1.4,
            color=SERIES_GREEN,
            zorder=6,
        )
    )
    axis.text(point[0] + label_offset[0], point[1] + label_offset[1], "穿透粒子",
              fontsize=8, color=TEXT_COLOR)
    axis.text(
        projected[0] + projected_label_offset[0],
        projected[1] + projected_label_offset[1],
        "投影位置",
        fontsize=8,
        color=TEXT_COLOR,
    )


def plot_sdf_collision_projection(output_dir: Path) -> list[Path]:
    grid = np.linspace(-1.4, 1.4, 420)
    x, y = np.meshgrid(grid, grid)
    levels = np.linspace(-0.65, 0.65, 14)
    figure, axes = plt.subplots(1, 2, figsize=(7.1, 3.35))

    sphere_radius = 0.72
    sphere_sdf = np.hypot(x, y) - sphere_radius
    sphere_point = np.array([0.34, 0.22])
    sphere_normal = sphere_point / np.linalg.norm(sphere_point)
    sphere_projected = sphere_normal * (sphere_radius + 0.025)

    half_x, half_y = 0.86, 0.56
    rectangle_sdf = box_sdf(x, y, half_x, half_y)
    box_point = np.array([0.72, 0.17])
    box_projected = np.array([half_x + 0.025, box_point[1]])

    cases = (
        (axes[0], sphere_sdf, sphere_point, sphere_projected, "(a) SphereCollider SDF"),
        (axes[1], rectangle_sdf, box_point, box_projected, "(b) BoxCollider SDF"),
    )
    for axis, sdf, point, projected, title in cases:
        contour = axis.contourf(x, y, sdf, levels=levels, cmap="coolwarm", alpha=0.74,
                                extend="both")
        axis.contour(x, y, sdf, levels=[0.0], colors=TEXT_COLOR, linewidths=1.6)
        axis.contour(x, y, sdf, levels=[-0.3, 0.3], colors="#666666", linewidths=0.6,
                     linestyles="--")
        projected_offset = (-0.42, 0.06) if title.startswith("(b)") else (0.03, 0.03)
        draw_projection(axis, point, projected, (-0.38, -0.14), projected_offset)
        axis.set_xlim(-1.25, 1.25)
        axis.set_ylim(-1.05, 1.05)
        axis.set_aspect("equal")
        axis.set_xlabel(r"局部坐标 $x$")
        axis.set_title(title)
        style_axes(axis)
        axis.grid(False)
    axes[0].set_ylabel(r"局部坐标 $y$")
    colorbar = figure.colorbar(contour, ax=axes, fraction=0.035, pad=0.03)
    colorbar.set_label("有符号距离")
    figure.subplots_adjust(left=0.07, right=0.90, bottom=0.15, top=0.88, wspace=0.18)
    return save_figure(figure, output_dir, "sdf_collision_projection")


def plot_trilinear_sdf_sampling(output_dir: Path) -> list[Path]:
    figure = plt.figure(figsize=(5.8, 4.25))
    axis = figure.add_subplot(111, projection="3d")
    corners = np.array(list(product((0.0, 1.0), repeat=3)))
    query = np.array([0.36, 0.58, 0.42])
    sample_values = np.linalg.norm(corners - np.array([0.55, 0.45, 0.50]), axis=1) - 0.42

    for start in corners:
        for dimension in range(3):
            if start[dimension] != 0.0:
                continue
            end = start.copy()
            end[dimension] = 1.0
            axis.plot(*zip(start, end, strict=True), color=SERIES_GRAY, linewidth=0.9)

    colors = colormaps["coolwarm"](
        (sample_values - sample_values.min()) / (np.ptp(sample_values) + 1e-12)
    )
    axis.scatter(corners[:, 0], corners[:, 1], corners[:, 2], s=48, c=colors,
                 edgecolors=TEXT_COLOR, linewidths=0.5, depthshade=False)
    for index, corner in enumerate(corners):
        axis.plot(
            [query[0], corner[0]],
            [query[1], corner[1]],
            [query[2], corner[2]],
            color=SERIES_BLUE,
            linewidth=0.55,
            alpha=0.42,
        )
        axis.text(corner[0] + 0.025, corner[1] + 0.025, corner[2] + 0.025,
                  rf"$s_{index}$", fontsize=7)
    axis.scatter(*query, s=90, color=SERIES_ORANGE, marker="*", depthshade=False)
    axis.text(query[0] + 0.04, query[1] + 0.04, query[2] + 0.05,
              r"查询点 $\mathbf{p}$", fontsize=8)
    axis.text2D(0.02, 0.02, "8个体素角点加权得到连续SDF值", transform=axis.transAxes,
                fontsize=8, color=SERIES_GRAY)
    axis.set_xlabel(r"$x$")
    axis.set_ylabel(r"$y$")
    axis.set_zlabel(r"$z$")
    axis.set_xticks([0, 1])
    axis.set_yticks([0, 1])
    axis.set_zticks([0, 1])
    axis.set_title("体素SDF的三线性插值采样")
    axis.view_init(elev=24, azim=35)
    axis.set_box_aspect((1, 1, 1))
    axis.grid(False)
    for pane in (axis.xaxis.pane, axis.yaxis.pane, axis.zaxis.pane):
        pane.set_alpha(0.0)
    figure.subplots_adjust(left=0.02, right=0.96, bottom=0.03, top=0.91)
    return save_figure(figure, output_dir, "mesh_sdf_trilinear_sampling")


def add_flow_node(
    axis: Axes,
    center: tuple[float, float],
    text: str,
    width: float,
    color: str,
) -> FancyBboxPatch:
    x, y = center
    patch = FancyBboxPatch(
        (x - width / 2, y - 0.19),
        width,
        0.38,
        boxstyle="round,pad=0.025,rounding_size=0.04",
        facecolor=color,
        edgecolor=TEXT_COLOR,
        linewidth=0.75,
    )
    axis.add_patch(patch)
    axis.text(x, y, text, ha="center", va="center", fontsize=7.5)
    return patch


def connect_nodes(axis: Axes, start: tuple[float, float], end: tuple[float, float],
                  color: str = SERIES_GRAY) -> None:
    axis.add_patch(
        FancyArrowPatch(
            start,
            end,
            arrowstyle="-|>",
            mutation_scale=9,
            linewidth=0.9,
            color=color,
        )
    )


def plot_solver_compute_pipelines(output_dir: Path) -> list[Path]:
    figure, axis = plt.subplots(figsize=(8.3, 3.35))
    axis.set_xlim(0, 10)
    axis.set_ylim(0, 4.25)
    axis.axis("off")

    axis.text(0.1, 3.30, "WCSPH", ha="left", va="center", fontsize=10)
    top_nodes = ((1.45, "Grid Build"), (3.55, "Density"), (5.65, "Force"),
                 (7.75, "Simulate"))
    for index, (x, label) in enumerate(top_nodes):
        add_flow_node(axis, (x, 3.30), label, 1.42,
                      LIGHT_ORANGE if label == "Force" else LIGHT_BLUE)
        if index:
            connect_nodes(axis, (top_nodes[index - 1][0] + 0.74, 3.30), (x - 0.74, 3.30))

    axis.text(0.1, 1.35, "PCISPH", ha="left", va="center", fontsize=10)
    bottom_nodes = (
        (1.15, "Grid Build"),
        (2.50, "Init"),
        (3.75, "Predict"),
        (5.05, "Density"),
        (6.45, "Pressure\nCorrection"),
        (7.85, "Force"),
        (9.10, "Apply"),
    )
    widths = (1.10, 0.85, 0.95, 0.95, 1.10, 0.90, 0.85)
    for index, ((x, label), width) in enumerate(zip(bottom_nodes, widths, strict=True)):
        loop_node = label in {"Predict", "Density", "Pressure\nCorrection", "Force"}
        add_flow_node(axis, (x, 1.35), label, width, LIGHT_ORANGE if loop_node else LIGHT_BLUE)
        if index:
            previous_x = bottom_nodes[index - 1][0]
            previous_width = widths[index - 1]
            connect_nodes(axis, (previous_x + previous_width / 2 + 0.03, 1.35),
                          (x - width / 2 - 0.03, 1.35))

    axis.add_patch(
        FancyArrowPatch(
            (7.85, 1.10),
            (3.75, 1.10),
            connectionstyle="arc3,rad=-0.34",
            arrowstyle="-|>",
            mutation_scale=10,
            linewidth=1.2,
            color=SERIES_ORANGE,
        )
    )
    axis.text(5.80, 0.40, r"预测—校正迭代 $K$ 次", ha="center", fontsize=8,
              color=SERIES_ORANGE)
    axis.text(4.60, 2.42, "单次前向求解", ha="center", fontsize=8, color=SERIES_GRAY)
    axis.add_patch(Rectangle((3.15, 0.82), 5.20, 1.06, fill=False,
                             edgecolor=SERIES_ORANGE, linewidth=0.8, linestyle="--"))
    figure.subplots_adjust(left=0.02, right=0.99, bottom=0.03, top=0.98)
    return save_figure(figure, output_dir, "wcsph_pcisph_compute_pipelines")


def plot_particle_lifecycle(output_dir: Path) -> list[Path]:
    figure, axis = plt.subplots(figsize=(6.6, 4.15))
    axis.set_xlim(-3.7, 3.7)
    axis.set_ylim(-2.45, 2.45)
    axis.set_aspect("equal")
    axis.axis("off")

    nodes = (
        ((-2.45, 1.35), "Dead List", LIGHT_BLUE),
        ((0.00, 2.00), "Emit", LIGHT_ORANGE),
        ((2.45, 1.35), "Alive List", LIGHT_GREEN),
        ((2.45, -1.25), "Simulate", LIGHT_BLUE),
        ((0.00, -2.00), "Expire", LIGHT_ORANGE),
        ((-2.45, -1.25), "Compact", LIGHT_GREEN),
    )
    width = 1.35
    for center, label, color in nodes:
        add_flow_node(axis, center, label, width, color)
    for index in range(len(nodes)):
        start = np.array(nodes[index][0], dtype=float)
        end = np.array(nodes[(index + 1) % len(nodes)][0], dtype=float)
        direction = end - start
        unit = direction / np.linalg.norm(direction)
        connect_nodes(axis, tuple(start + unit * 0.73), tuple(end - unit * 0.73), SERIES_BLUE)

    pool = Circle((0, 0), 0.80, facecolor="#F4F4F4", edgecolor=TEXT_COLOR, linewidth=0.9)
    axis.add_patch(pool)
    axis.text(0, 0.12, "固定容量", ha="center", va="center", fontsize=8)
    axis.text(0, -0.16, "Particle Pool", ha="center", va="center", fontsize=9)
    axis.text(-3.35, 0.05, "槽位复用", rotation=90, ha="center", va="center",
              fontsize=8, color=SERIES_GRAY)
    axis.text(3.32, 0.05, "仅处理存活粒子", rotation=-90, ha="center", va="center",
              fontsize=8, color=SERIES_GRAY)
    figure.subplots_adjust(left=0.02, right=0.98, bottom=0.02, top=0.98)
    return save_figure(figure, output_dir, "particle_lifecycle_recycling")


def main() -> int:
    configure_matplotlib()
    output_dir = PROJECT_DIR / "generated"
    generated: list[Path] = []
    generated.extend(plot_sph_interpolation_neighborhood(output_dir))
    generated.extend(plot_spatial_hash_neighborhood(output_dir))
    generated.extend(plot_pressure_equation_of_state(output_dir))
    generated.extend(plot_sdf_collision_projection(output_dir))
    generated.extend(plot_trilinear_sdf_sampling(output_dir))
    generated.extend(plot_solver_compute_pipelines(output_dir))
    generated.extend(plot_particle_lifecycle(output_dir))
    for path in generated:
        print(path.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
