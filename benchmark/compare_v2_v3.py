import csv
import sys
from pathlib import Path

root = Path(r"C:/Dev/Workspace/C++/Graduation_Project")

# 用法: python compare_v2_v3.py [baseline_summary.csv] [target_summary.csv]
# 默认: results_v2（8/22 论文口径基线） vs run_20260831_174549（空调房定稿轮）
DEFAULT_BASELINE = "results_v2/summary.csv"
DEFAULT_TARGET = "benchmark/results/run_20260831_174549/summary.csv"


def load(path):
    rows = {}
    with open(path, newline="", encoding="utf-8-sig") as f:
        for r in csv.DictReader(f):
            rows[(r["Backend"], r["Solver"], int(r["Particles"]))] = r
    return rows


v2 = load(root / (sys.argv[1] if len(sys.argv) > 1 else DEFAULT_BASELINE))
v3 = load(root / (sys.argv[2] if len(sys.argv) > 2 else DEFAULT_TARGET))

print(f"{'Backend':8} {'Solver':7} {'Part':>6} | {'v2 ms':>10} {'v3 ms':>10} {'delta%':>8} | {'v2 speed':>8} {'v3 speed':>8}")
print("-" * 78)
for solver in ("WCSPH", "PCISPH"):
    for particles in (1000, 5000, 10000, 20000, 50000):
        for backend in ("OpenGL", "CUDA", "Vulkan"):
            key = (backend, solver, particles)
            if key not in v2 or key not in v3:
                continue
            m2, m3 = float(v2[key]["Mean_ms"]), float(v3[key]["Mean_ms"])
            s2 = v2[key]["Speedup"]
            s3 = v3[key]["Speedup"]
            s2 = f"{float(s2):.3f}" if s2 else "-"
            s3 = f"{float(s3):.3f}" if s3 else "-"
            delta = (m3 - m2) / m2 * 100.0
            mark = "  <==" if backend == "Vulkan" and abs(delta) > 10 else ""
            print(f"{backend:8} {solver:7} {particles:>6} | {m2:>10.3f} {m3:>10.3f} {delta:>+7.1f}% | {s2:>8} {s3:>8}{mark}")
    print()
