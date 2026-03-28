# Task-Based Parallel Runtime with Work-Stealing Scheduler

`task-runtime-work-stealing` is a compact HPC research project centered on a C++17 task runtime for DAG execution with a work-stealing scheduler.

## Overview

The project includes:

- `include/` and `src/`: C++17 task runtime with a work-stealing scheduler
- `benchmarks/`: benchmark driver for parallel sum, matrix multiplication, and synthetic DAG workloads
- `results/`: generated CSV files and figures
- `scripts/`: plotting utilities
- `docs/`: GitHub Pages publish target
- `website/`: static site assets for GitHub Pages

The runtime executes tasks represented as a directed acyclic graph, uses a fixed-size `std::thread` pool, balances load with per-worker queues plus work stealing, and reports execution time, speedup, and efficiency across multiple workloads.

## Repository

- GitHub: `https://github.com/iamnotvk/task-runtime-work-stealing`

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

It also syncs the GitHub Pages figures into `docs/assets/figures/`.

## Website

To publish the site with GitHub Pages:

1. Open repository `Settings`
2. Open `Pages`
3. Set `Source` to `Deploy from a branch`
4. Select branch `master`
5. Select folder `/docs`
6. Save

## Benchmarks

The benchmark suite currently evaluates:

- `parallel_sum`
- `matrix_multiplication`
- `synthetic_dag`

Thread counts:

- `1`
- `2`
- `4`
- `8`

Metrics:

- execution time
- speedup
- efficiency

## Notes

- The paper source is kept local-only and is not tracked in the public repository.
- If `cmake` is not available on your machine, you can still compile directly with `clang++` or `g++`.
