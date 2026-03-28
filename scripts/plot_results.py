from pathlib import Path
import csv
import os
import shutil

ROOT = Path(__file__).resolve().parents[1]
RESULTS_CSV = ROOT / "results" / "benchmark_results.csv"
FIGURES_DIR = ROOT / "results" / "figures"
WEBSITE_FIGURES_DIR = ROOT / "website" / "assets" / "figures"
MPL_DIR = ROOT / ".cache" / "matplotlib"
XDG_CACHE_DIR = ROOT / ".cache"

MPL_DIR.mkdir(parents=True, exist_ok=True)
XDG_CACHE_DIR.mkdir(parents=True, exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", str(MPL_DIR))
os.environ.setdefault("XDG_CACHE_HOME", str(XDG_CACHE_DIR))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load_rows():
    rows = []
    with RESULTS_CSV.open() as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            rows.append(
                {
                    "workload": row["workload"],
                    "threads": int(row["threads"]),
                    "execution_ms": float(row["execution_ms"]),
                    "speedup": float(row["speedup"]),
                    "efficiency": float(row["efficiency"]),
                }
            )
    return rows


def plot_metric(rows, metric, ylabel, filename):
    workloads = sorted({row["workload"] for row in rows})
    FIGURES_DIR.mkdir(parents=True, exist_ok=True)
    WEBSITE_FIGURES_DIR.mkdir(parents=True, exist_ok=True)

    plt.figure(figsize=(7, 4.5))
    for workload in workloads:
        subset = sorted(
            [row for row in rows if row["workload"] == workload],
            key=lambda item: item["threads"],
        )
        plt.plot(
            [row["threads"] for row in subset],
            [row[metric] for row in subset],
            marker="o",
            linewidth=2,
            label=workload.replace("_", " "),
        )

    plt.xlabel("Threads")
    plt.ylabel(ylabel)
    plt.xticks([1, 2, 4, 8])
    plt.grid(True, linestyle="--", alpha=0.4)
    plt.legend(frameon=False)
    plt.tight_layout()
    output_path = FIGURES_DIR / filename
    plt.savefig(output_path, dpi=180)
    plt.close()
    shutil.copy2(output_path, WEBSITE_FIGURES_DIR / filename)


def main():
    rows = load_rows()
    plot_metric(rows, "execution_ms", "Execution Time (ms)", "execution_time.png")
    plot_metric(rows, "speedup", "Speedup", "speedup.png")
    plot_metric(rows, "efficiency", "Efficiency", "efficiency.png")


if __name__ == "__main__":
    main()
