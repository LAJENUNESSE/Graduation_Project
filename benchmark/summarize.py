#!/usr/bin/env python3
"""Summarize raw fluid benchmark samples using only the Python standard library."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path


REQUIRED_COLUMNS = {
    "Backend",
    "Solver",
    "Particles",
    "Iterations",
    "Run",
    "SampleValid",
    "Compute_ms",
    "Interop_ms",
    "EndToEnd_ms",
    "AliveCount",
    "MeanDensity",
    "MaxDensityError",
    "RMSDensityError",
    "OutOfBoundsCount",
}


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def parse_finite(row: dict[str, str], column: str) -> float:
    value = float(row[column])
    if not math.isfinite(value):
        raise ValueError(f"non-finite {column}: {row[column]!r}")
    return value


def is_sample_valid(value: str) -> bool:
    return value.strip().lower() in {"1", "true", "yes"}


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize fluid benchmark raw CSV data.")
    parser.add_argument("raw_csv", type=Path, help="Merged raw_results.csv")
    parser.add_argument("--output", type=Path, help="Summary CSV path (default: beside raw CSV)")
    parser.add_argument(
        "--density-relative-tolerance",
        type=float,
        help=(
            "Enable speedup output only when each target backend's mean density differs from OpenGL "
            "by no more than this relative tolerance (for example 0.05)."
        ),
    )
    parser.add_argument(
        "--max-density-error-tolerance",
        type=float,
        help=(
            "Additionally require each target backend's worst per-sample MaxDensityError (L∞ of |rho-rho0|) "
            "to stay within this absolute tolerance of the OpenGL baseline's own worst value (for example 5.0)."
        ),
    )
    args = parser.parse_args()

    if args.density_relative_tolerance is not None and args.density_relative_tolerance < 0.0:
        parser.error("--density-relative-tolerance must be non-negative")
    if args.max_density_error_tolerance is not None and args.max_density_error_tolerance < 0.0:
        parser.error("--max-density-error-tolerance must be non-negative")

    output_path = args.output or args.raw_csv.with_name("summary.csv")
    groups: dict[tuple[str, str, int, int], list[dict[str, str]]] = defaultdict(list)

    with args.raw_csv.open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        missing = REQUIRED_COLUMNS.difference(reader.fieldnames or [])
        if missing:
            raise ValueError(f"raw CSV is missing columns: {', '.join(sorted(missing))}")
        for row in reader:
            if not is_sample_valid(row["SampleValid"]):
                continue
            key = (row["Backend"], row["Solver"], int(row["Particles"]), int(row["Iterations"]))
            groups[key].append(row)

    if not groups:
        raise ValueError("raw CSV contains no valid benchmark samples")

    summaries: dict[tuple[str, str, int, int], dict[str, object]] = {}
    for key, rows in groups.items():
        compute_values = [parse_finite(row, "Compute_ms") for row in rows]
        if any(value <= 0.0 for value in compute_values):
            raise ValueError(f"group {key} contains a non-positive Compute_ms sample")
        interop_values = [parse_finite(row, "Interop_ms") for row in rows]
        end_to_end_values = [parse_finite(row, "EndToEnd_ms") for row in rows]
        if any(value < 0.0 for value in interop_values):
            raise ValueError(f"group {key} contains a negative Interop_ms sample")
        if any(value <= 0.0 for value in end_to_end_values):
            raise ValueError(f"group {key} contains a non-positive EndToEnd_ms sample")

        particles = key[2]
        alive_valid = all(int(row["AliveCount"]) == particles for row in rows)
        out_of_bounds_values = [int(row["OutOfBoundsCount"]) for row in rows]
        bounds_valid = all(value == 0 for value in out_of_bounds_values)
        density_values = [parse_finite(row, "MeanDensity") for row in rows]
        density_errors = [parse_finite(row, "MaxDensityError") for row in rows]
        rms_errors = [parse_finite(row, "RMSDensityError") for row in rows]
        correctness_valid = alive_valid and bounds_valid and all(
            value >= 0.0 for value in density_errors + rms_errors
        )

        summaries[key] = {
            "Backend": key[0],
            "Solver": key[1],
            "Particles": particles,
            "Iterations": key[3],
            "Samples": len(compute_values),
            "Mean_ms": statistics.fmean(compute_values),
            "StdDev_ms": statistics.stdev(compute_values) if len(compute_values) > 1 else 0.0,
            "P50_ms": percentile(compute_values, 0.50),
            "P95_ms": percentile(compute_values, 0.95),
            "Min_ms": min(compute_values),
            "Max_ms": max(compute_values),
            "MeanInterop_ms": statistics.fmean(interop_values),
            "P95Interop_ms": percentile(interop_values, 0.95),
            "MeanEndToEnd_ms": statistics.fmean(end_to_end_values),
            "P95EndToEnd_ms": percentile(end_to_end_values, 0.95),
            "MeanDensity": statistics.fmean(density_values),
            "MaxDensityError": max(density_errors),
            "RMSDensityError": max(rms_errors),
            "MaxOutOfBoundsCount": max(out_of_bounds_values),
            "CorrectnessValid": correctness_valid,
            "Speedup": "",
        }

    tolerance = args.density_relative_tolerance
    linf_tolerance = args.max_density_error_tolerance
    for key, summary in summaries.items():
        baseline_key = ("OpenGL", key[1], key[2], key[3])
        baseline = summaries.get(baseline_key)
        if (tolerance is None and linf_tolerance is None) or baseline is None:
            continue
        if not summary["CorrectnessValid"] or not baseline["CorrectnessValid"]:
            continue

        # 均值门禁：跨后端平均密度相对偏差（原有）
        if tolerance is not None:
            baseline_density = float(baseline["MeanDensity"])
            target_density = float(summary["MeanDensity"])
            density_scale = max(abs(baseline_density), 1.0e-12)
            if abs(target_density - baseline_density) / density_scale > tolerance:
                summary["CorrectnessValid"] = False
                continue

        # L∞ 门禁：逐样本最大密度偏差不得显著劣于 OpenGL 基线自身水平，
        # 拦截"均值合格但局部不稳定"的运行
        if linf_tolerance is not None:
            target_linf = float(summary["MaxDensityError"])
            baseline_linf = float(baseline["MaxDensityError"])
            if abs(target_linf - baseline_linf) > linf_tolerance:
                summary["CorrectnessValid"] = False
                continue

        summary["Speedup"] = float(baseline["Mean_ms"]) / float(summary["Mean_ms"])

    output_path.parent.mkdir(parents=True, exist_ok=True)
    columns = [
        "Backend",
        "Solver",
        "Particles",
        "Iterations",
        "Samples",
        "Mean_ms",
        "StdDev_ms",
        "P50_ms",
        "P95_ms",
        "Min_ms",
        "Max_ms",
        "MeanInterop_ms",
        "P95Interop_ms",
        "MeanEndToEnd_ms",
        "P95EndToEnd_ms",
        "MeanDensity",
        "MaxDensityError",
        "RMSDensityError",
        "MaxOutOfBoundsCount",
        "CorrectnessValid",
        "Speedup",
    ]
    with output_path.open("w", encoding="utf-8", newline="") as destination:
        writer = csv.DictWriter(destination, fieldnames=columns)
        writer.writeheader()
        for key in sorted(summaries, key=lambda item: (item[1], item[2], item[3], item[0])):
            writer.writerow(summaries[key])

    print(f"Wrote {len(summaries)} summary groups to {output_path}")
    if tolerance is None:
        print(
            "Speedup is intentionally blank until --density-relative-tolerance is supplied after the short "
            "cross-backend calibration run.",
            file=sys.stderr,
        )
    if linf_tolerance is None:
        print(
            "L∞ gate inactive; pass --max-density-error-tolerance to reject runs whose worst per-sample "
            "density error exceeds the OpenGL baseline beyond the given absolute tolerance.",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
