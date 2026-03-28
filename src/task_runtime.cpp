#include "hpc/task_runtime.h"

#include <algorithm>
#include <stdexcept>

namespace hpc {

TaskRuntime::TaskRuntime(std::size_t thread_count)
    : queues_(std::max<std::size_t>(1, thread_count)) {
    for (std::size_t worker_id = 0; worker_id < queues_.size(); ++worker_id) {
        workers_.emplace_back([this, worker_id] {
            worker_loop(worker_id);
        });
    }
}

TaskRuntime::~TaskRuntime() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        stopping_ = true;
    }
    ready_cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

TaskRuntime::TaskId TaskRuntime::create_task(TaskFunction fn) {
    if (scheduled_) {
        throw std::logic_error("cannot add tasks after scheduling");
    }

    tasks_.emplace_back();
    tasks_.back().fn = std::move(fn);
    return TaskId{tasks_.size() - 1};
}

void TaskRuntime::add_dependency(TaskId predecessor, TaskId successor) {
    if (scheduled_) {
        throw std::logic_error("cannot add dependencies after scheduling");
    }

    if (predecessor.value >= tasks_.size() || successor.value >= tasks_.size()) {
        throw std::out_of_range("invalid task id");
    }

    tasks_[predecessor.value].successors.push_back(successor.value);
    tasks_[successor.value].remaining_dependencies.fetch_add(1, std::memory_order_relaxed);
}

void TaskRuntime::schedule() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (scheduled_) {
            throw std::logic_error("schedule already called");
        }
        scheduled_ = true;
    }

    if (tasks_.empty()) {
        finished_cv_.notify_all();
        return;
    }

    for (std::size_t task_id = 0; task_id < tasks_.size(); ++task_id) {
        if (tasks_[task_id].remaining_dependencies.load(std::memory_order_acquire) == 0) {
            enqueue_task(task_id, next_worker_.fetch_add(1, std::memory_order_relaxed) % queues_.size());
        }
    }

    ready_cv_.notify_all();
}

void TaskRuntime::wait() {
    std::unique_lock<std::mutex> lock(state_mutex_);
    finished_cv_.wait(lock, [this] {
        return completed_tasks_.load(std::memory_order_acquire) == tasks_.size();
    });
}

std::size_t TaskRuntime::thread_count() const noexcept {
    return queues_.size();
}

void TaskRuntime::worker_loop(std::size_t worker_id) {
    std::mt19937 generator(static_cast<std::mt19937::result_type>(worker_id + 17));

    while (true) {
        std::size_t task_id = 0;
        if (pop_local(worker_id, task_id) || steal_task(worker_id, generator, task_id)) {
            run_task(worker_id, task_id);
            continue;
        }

        std::unique_lock<std::mutex> lock(state_mutex_);
        ready_cv_.wait(lock, [this, worker_id] {
            if (stopping_) {
                return true;
            }

            if (!scheduled_) {
                return false;
            }

            if (!queues_[worker_id].tasks.empty()) {
                return true;
            }

            return active_tasks_.load(std::memory_order_acquire) > 0 ||
                   completed_tasks_.load(std::memory_order_acquire) < tasks_.size();
        });

        if (stopping_) {
            return;
        }
    }
}

bool TaskRuntime::pop_local(std::size_t worker_id, std::size_t& task_id) {
    auto& queue = queues_[worker_id];
    std::lock_guard<std::mutex> lock(queue.mutex);
    if (queue.tasks.empty()) {
        return false;
    }

    task_id = queue.tasks.back();
    queue.tasks.pop_back();
    return true;
}

bool TaskRuntime::steal_task(std::size_t worker_id, std::mt19937& generator, std::size_t& task_id) {
    if (queues_.size() == 1) {
        return false;
    }

    std::uniform_int_distribution<std::size_t> distribution(0, queues_.size() - 1);
    for (std::size_t attempt = 0; attempt < queues_.size() * 2; ++attempt) {
        const std::size_t victim = distribution(generator);
        if (victim == worker_id) {
            continue;
        }

        auto& queue = queues_[victim];
        std::lock_guard<std::mutex> lock(queue.mutex);
        if (queue.tasks.empty()) {
            continue;
        }

        task_id = queue.tasks.front();
        queue.tasks.pop_front();
        return true;
    }

    return false;
}

void TaskRuntime::enqueue_task(std::size_t task_id, std::size_t preferred_worker) {
    auto& queue = queues_[preferred_worker % queues_.size()];
    {
        std::lock_guard<std::mutex> lock(queue.mutex);
        queue.tasks.push_back(task_id);
    }
    ready_cv_.notify_one();
}

void TaskRuntime::run_task(std::size_t worker_id, std::size_t task_id) {
    active_tasks_.fetch_add(1, std::memory_order_acq_rel);
    tasks_[task_id].fn();

    for (const std::size_t successor : tasks_[task_id].successors) {
        const std::size_t previous =
            tasks_[successor].remaining_dependencies.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 1) {
            enqueue_task(successor, worker_id);
        }
    }

    active_tasks_.fetch_sub(1, std::memory_order_acq_rel);
    const std::size_t finished = completed_tasks_.fetch_add(1, std::memory_order_acq_rel) + 1;

    if (finished == tasks_.size()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        finished_cv_.notify_all();
        ready_cv_.notify_all();
    } else {
        ready_cv_.notify_all();
    }
}

}  // namespace hpc
