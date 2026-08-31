import csv
from pathlib import Path

root = Path(r"C:/Dev/Workspace/C++/Graduation_Project")


def load(path):
    rows = {}
    with open(path, newline="", encoding="utf-8-sig") as f:
        for r in csv.DictReader(f):
            rows[(r["Backend"], r["Solver"], int(r["Particles"]))] = r
    return rows


v2 = load(root / "benchmark/results/run_20260822_223630/summary.csv")
v3 = load(root / "results_v3/run_20260831_110725/summary.csv")

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
