#include "hpc/task_runtime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::high_resolution_clock;

struct BenchmarkRow {
    std::string workload;
    std::size_t threads = 0;
    double execution_ms = 0.0;
    double speedup = 0.0;
    double efficiency = 0.0;
};

double elapsed_ms(const Clock::time_point& start, const Clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double median_runtime(std::vector<double> samples) {
    if (samples.empty()) {
        throw std::invalid_argument("samples must not be empty");
    }

    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

std::vector<std::uint64_t> build_data(std::size_t count) {
    std::vector<std::uint64_t> data(count);
    for (std::size_t i = 0; i < count; ++i) {
        data[i] = static_cast<std::uint64_t>((i % 97) + 1);
    }
    return data;
}

double benchmark_parallel_sum(std::size_t threads) {
    constexpr std::size_t total_values = 8'000'000;
    constexpr std::size_t chunk_size = 125'000;

    auto data = build_data(total_values);
    const std::size_t chunk_count = (total_values + chunk_size - 1) / chunk_size;
    std::vector<std::uint64_t> partials(chunk_count, 0);
    std::vector<std::uint64_t> reductions(chunk_count * 2, 0);

    hpc::TaskRuntime runtime(threads);
    struct ReductionNode {
        hpc::TaskRuntime::TaskId task_id;
        std::size_t slot = 0;
    };

    std::vector<ReductionNode> current_level;
    current_level.reserve(chunk_count);

    for (std::size_t chunk = 0; chunk < chunk_count; ++chunk) {
        const std::size_t begin = chunk * chunk_size;
        const std::size_t end = std::min(begin + chunk_size, total_values);

        auto task_id = runtime.create_task([&, chunk, begin, end] {
            std::uint64_t local_sum = 0;
            for (std::size_t i = begin; i < end; ++i) {
                local_sum += data[i];
            }
            partials[chunk] = local_sum;
            reductions[chunk] = local_sum;
        });
        current_level.push_back(ReductionNode{task_id, chunk});
    }

    std::size_t next_slot = chunk_count;
    while (current_level.size() > 1) {
        std::vector<ReductionNode> next_level;
        next_level.reserve((current_level.size() + 1) / 2);

        for (std::size_t i = 0; i < current_level.size(); i += 2) {
            if (i + 1 >= current_level.size()) {
                next_level.push_back(current_level[i]);
                continue;
            }

            const std::size_t left = current_level[i].slot;
            const std::size_t right = current_level[i + 1].slot;
            const std::size_t output = next_slot++;

            auto reduce_task = runtime.create_task([&, left, right, output] {
                reductions[output] = reductions[left] + reductions[right];
            });

            runtime.add_dependency(current_level[i].task_id, reduce_task);
            runtime.add_dependency(current_level[i + 1].task_id, reduce_task);
            next_level.push_back(ReductionNode{reduce_task, output});
        }
        current_level = std::move(next_level);
    }

    const auto start = Clock::now();
    runtime.schedule();
    runtime.wait();
    const auto end = Clock::now();

    const std::uint64_t expected = std::accumulate(data.begin(), data.end(), std::uint64_t{0});
    if (reductions[current_level.front().slot] != expected) {
        throw std::runtime_error("parallel sum verification failed");
    }

    return elapsed_ms(start, end);
}

double benchmark_matrix_multiply(std::size_t threads) {
    constexpr std::size_t n = 256;
    constexpr std::size_t block_rows = 16;

    std::vector<double> a(n * n);
    std::vector<double> b(n * n);
    std::vector<double> c(n * n, 0.0);

    for (std::size_t i = 0; i < n * n; ++i) {
        a[i] = static_cast<double>((i % 11) + 1);
        b[i] = static_cast<double>((i % 17) + 1);
    }

    hpc::TaskRuntime runtime(threads);
    for (std::size_t row = 0; row < n; row += block_rows) {
        const std::size_t row_end = std::min(row + block_rows, n);
        runtime.create_task([&, row, row_end] {
            for (std::size_t i = row; i < row_end; ++i) {
                for (std::size_t k = 0; k < n; ++k) {
                    const double a_val = a[i * n + k];
                    for (std::size_t j = 0; j < n; ++j) {
                        c[i * n + j] += a_val * b[k * n + j];
                    }
                }
            }
        });
    }

    const auto start = Clock::now();
    runtime.schedule();
    runtime.wait();
    const auto end = Clock::now();

    double checksum = 0.0;
    for (double value : c) {
        checksum += value;
    }
    if (checksum <= 0.0) {
        throw std::runtime_error("matrix multiply verification failed");
    }

    return elapsed_ms(start, end);
}

double benchmark_synthetic_dag(std::size_t threads) {
    constexpr std::size_t stages = 10;
    constexpr std::size_t width = 32;
    constexpr std::uint64_t iterations = 80'000;

    std::vector<std::uint64_t> values(stages * width, 0);
    hpc::TaskRuntime runtime(threads);
    std::vector<std::vector<hpc::TaskRuntime::TaskId>> levels(stages);

    for (std::size_t stage = 0; stage < stages; ++stage) {
        levels[stage].reserve(width);
        for (std::size_t lane = 0; lane < width; ++lane) {
            auto task = runtime.create_task([&, stage, lane] {
                std::uint64_t acc = stage + lane + 1;
                for (std::uint64_t it = 0; it < iterations; ++it) {
                    acc = (acc * 1'664'525ULL + 1'013'904'223ULL + it) % 1'000'000'007ULL;
                }
                if (stage > 0) {
                    acc += values[(stage - 1) * width + lane];
                    if (lane > 0) {
                        acc += values[(stage - 1) * width + (lane - 1)];
                    }
                }
                values[stage * width + lane] = acc;
            });

            levels[stage].push_back(task);

            if (stage > 0) {
                runtime.add_dependency(levels[stage - 1][lane], task);
                if (lane > 0) {
                    runtime.add_dependency(levels[stage - 1][lane - 1], task);
                }
            }
        }
    }

    const auto start = Clock::now();
    runtime.schedule();
    runtime.wait();
    const auto end = Clock::now();

    const std::uint64_t tail = values.back();
    if (tail == 0) {
        throw std::runtime_error("synthetic dag verification failed");
    }

    return elapsed_ms(start, end);
}

void write_csv(const std::filesystem::path& output_path, const std::vector<BenchmarkRow>& rows) {
    std::filesystem::create_directories(output_path.parent_path());
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("failed to open benchmark output file");
    }

    out << "workload,threads,execution_ms,speedup,efficiency\n";
    out << std::fixed << std::setprecision(4);
    for (const auto& row : rows) {
        out << row.workload << ','
            << row.threads << ','
            << row.execution_ms << ','
            << row.speedup << ','
            << row.efficiency << '\n';
    }
}

}  // namespace

int main() {
    const std::vector<std::size_t> thread_counts{1, 2, 4, 8};
    constexpr std::size_t trials = 3;
    const std::filesystem::path output_path = "results/benchmark_results.csv";

    struct Workload {
        std::string name;
        double (*run)(std::size_t);
    };

    const std::vector<Workload> workloads{
        {"parallel_sum", benchmark_parallel_sum},
        {"matrix_multiplication", benchmark_matrix_multiply},
        {"synthetic_dag", benchmark_synthetic_dag},
    };

    std::vector<BenchmarkRow> rows;
    rows.reserve(workloads.size() * thread_counts.size());

    for (const auto& workload : workloads) {
        double baseline = 0.0;
        for (std::size_t threads : thread_counts) {
            std::vector<double> samples;
            samples.reserve(trials);
            for (std::size_t trial = 0; trial < trials; ++trial) {
                samples.push_back(workload.run(threads));
            }

            const double runtime_ms = median_runtime(samples);
            if (threads == 1) {
                baseline = runtime_ms;
            }

            rows.push_back(BenchmarkRow{
                workload.name,
                threads,
                runtime_ms,
                baseline / runtime_ms,
                (baseline / runtime_ms) / static_cast<double>(threads),
            });

            std::cout << workload.name << " threads=" << threads
                      << " time_ms=" << std::fixed << std::setprecision(4) << runtime_ms
                      << " trials=" << trials
                      << '\n';
        }
    }

    write_csv(output_path, rows);
    std::cout << "wrote results to " << output_path << '\n';
    return 0;
}
