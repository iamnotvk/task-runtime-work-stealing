# Task-Based Parallel Runtime with Work-Stealing Scheduler

This repository contains a complete research-style HPC project:

- `include/` and `src/`: C++17 task runtime with a work-stealing scheduler
- `benchmarks/`: benchmark driver for parallel sum, matrix multiplication, and synthetic DAG workloads
- `results/`: generated CSV files and figures
- `scripts/`: plotting utilities
- `paper/`: LaTeX preprint
- `website/`: static site assets for GitHub Pages

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run Benchmarks

```bash
./build/hpc_benchmarks
```

This writes `results/benchmark_results.csv`.

## Generate Figures

```bash
.venv/bin/python3 scripts/plot_results.py
```

This writes PNG figures to `results/figures/` and copies website-ready versions to `website/assets/figures/`.
