#pragma once
#include "metastore/metastore.h"
#include "query/planner/planner.h"
#include "query/registry/query_registry.h"
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace isbd {

class Executor {
public:
    Executor(Metastore& metastore, QueryRegistry& registry, size_t num_workers = 0);
    ~Executor();

    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    // Enqueues a validated ExecutionPlan for asynchronous background execution.
    // Returns immediately without blocking the calling HTTP thread.
    void submit(ExecutionPlan plan);

    // Gracefully drains queued jobs and joins all worker threads.
    void shutdown();

private:
    void worker_loop();
    void execute_copy(const CopyPlan& plan);
    void execute_select(const SelectPlan& plan);

    Metastore& metastore_;
    QueryRegistry& registry_;

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<ExecutionPlan> task_queue_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

} // namespace isbd