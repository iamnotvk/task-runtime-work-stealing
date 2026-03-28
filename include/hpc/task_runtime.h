#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace hpc {

class TaskRuntime {
public:
    using TaskFunction = std::function<void()>;

    struct TaskId {
        std::size_t value = 0;
    };

    explicit TaskRuntime(std::size_t thread_count);
    ~TaskRuntime();

    TaskId create_task(TaskFunction fn);
    void add_dependency(TaskId predecessor, TaskId successor);

    void schedule();
    void wait();

    std::size_t thread_count() const noexcept;

private:
    struct TaskNode {
        TaskFunction fn;
        std::vector<std::size_t> successors;
        std::atomic<std::size_t> remaining_dependencies{0};
    };

    struct WorkerQueue {
        std::deque<std::size_t> tasks;
        mutable std::mutex mutex;
    };

    void worker_loop(std::size_t worker_id);
    bool pop_local(std::size_t worker_id, std::size_t& task_id);
    bool steal_task(std::size_t worker_id, std::mt19937& generator, std::size_t& task_id);
    void enqueue_task(std::size_t task_id, std::size_t preferred_worker);
    void run_task(std::size_t worker_id, std::size_t task_id);

    std::deque<TaskNode> tasks_;
    std::vector<WorkerQueue> queues_;
    std::vector<std::thread> workers_;

    mutable std::mutex state_mutex_;
    std::condition_variable ready_cv_;
    std::condition_variable finished_cv_;

    std::atomic<std::size_t> next_worker_{0};
    std::atomic<std::size_t> completed_tasks_{0};
    std::atomic<std::size_t> active_tasks_{0};

    bool scheduled_ = false;
    bool stopping_ = false;
};

}  // namespace hpc
