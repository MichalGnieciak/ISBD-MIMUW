#include <algorithm>
#include "csv/csv_parser.h"
#include "deserializer.h"
#include "executor.h"
#include <filesystem>
#include <stdexcept>

namespace isbd {

Executor::Executor(Metastore& metastore, QueryRegistry& registry, size_t num_workers)
    : metastore_(metastore), registry_(registry) {
    if (num_workers == 0) {
        unsigned int hw = std::thread::hardware_concurrency();
        num_workers = std::max<size_t>(2, hw);
    }

    workers_.reserve(num_workers);
    for (size_t i = 0; i < num_workers; ++i) {
        workers_.emplace_back(&Executor::worker_loop, this);
    }
}

Executor::~Executor() {
    shutdown();
}

void Executor::submit(ExecutionPlan plan) {
    {
        std::lock_guard lock(queue_mutex_);
        if (stopping_) {
            throw std::runtime_error("Cannot submit query: Executor is shutting down");
        }
        task_queue_.push(std::move(plan));
    }
    queue_cv_.notify_one();
}

void Executor::shutdown() {
    {
        std::lock_guard lock(queue_mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
    }
    queue_cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void Executor::worker_loop() {
    while (true) {
        ExecutionPlan plan = [&]() -> ExecutionPlan {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return stopping_ || !task_queue_.empty(); });

            if (stopping_ && task_queue_.empty()) {
                return CopyPlan{};
            }
            ExecutionPlan next = std::move(task_queue_.front());
            task_queue_.pop();
            return next;
        }();

        // Exit when stopping_ is set and queue is drained
        {
            std::lock_guard lock(queue_mutex_);
            if (stopping_ && task_queue_.empty() &&
                std::holds_alternative<CopyPlan>(plan) &&
                std::get<CopyPlan>(plan).query_id.empty()) {
                return;
            }
        }

        if (std::holds_alternative<CopyPlan>(plan)) {
            execute_copy(std::get<CopyPlan>(plan));
        } else {
            execute_select(std::get<SelectPlan>(plan));
        }
    }
}

void Executor::execute_copy(const CopyPlan& plan) {
    registry_.set_status(plan.query_id, QueryStatus::RUNNING);

    try {
        TableMetadata table_view;
        table_view.table_id = plan.table_id;
        table_view.name = plan.table_name;
        table_view.columns = plan.table_schema;

        CopyOptions options{
            plan.source_csv_path,
            plan.does_csv_contain_header,
            {} // Already validated
        };

        // Reconstruct destinationColumns names in CSV order so CsvParser uses the exact validated mapping
        options.destination_columns.reserve(plan.csv_to_table_idx.size());
        for (size_t table_idx : plan.csv_to_table_idx) {
            options.destination_columns.push_back(plan.table_schema[table_idx].name);
        }

        CopyResult copy_res = CsvParser::parse_to_isbd(table_view, options, plan.target_isbd_path);

        // Atomically register the newly written .isbd file in Metastore
        bool committed = metastore_.commit_copy_files(plan.table_id, {plan.target_isbd_path});
        if (!committed) {
            // Table was deleted concurrently while COPY was running — clean up orphan file
            std::error_code ec;
            std::filesystem::remove(plan.target_isbd_path, ec);
            registry_.fail_query(
                plan.query_id,
                "Target table '" + plan.table_name + "' was deleted while COPY was executing");
            return;
        }

        registry_.complete_copy(plan.query_id, copy_res.rows_ingested);
    } catch (const std::exception& ex) {
        std::error_code ec;
        std::filesystem::remove(plan.target_isbd_path, ec);
        registry_.fail_query(plan.query_id, ex.what());
    }
}

void Executor::execute_select(const SelectPlan& plan) {
    registry_.set_status(plan.query_id, QueryStatus::RUNNING);

    try {
        SelectResultData result;
        result.schema = plan.output_schema;
        result.total_rows = 0;

        // Check if every requested column index is distinct (enables zero-copy std::move)
        std::vector<bool> seen(plan.projected_col_indices.size(), false);
        bool has_duplicate_projections = false;
        {
            std::unordered_map<size_t, size_t> counts;
            for (size_t idx : plan.projected_col_indices) {
                if (++counts[idx] > 1) {
                    has_duplicate_projections = true;
                    break;
                }
            }
        }

        for (const auto& managed_file : plan.files) {
            Deserializer deserializer(managed_file->path().string());

            while (deserializer.has_next_batch()) {
                Batch raw_batch = deserializer.read_next_batch();
                if (raw_batch.num_rows == 0) {
                    continue;
                }

                Batch projected_batch;
                projected_batch.num_rows = raw_batch.num_rows;
                projected_batch.columns.reserve(plan.projected_col_indices.size());

                for (size_t src_col_idx : plan.projected_col_indices) {
                    if (has_duplicate_projections) {
                        projected_batch.columns.push_back(raw_batch.columns[src_col_idx]);
                    } else {
                        projected_batch.columns.push_back(std::move(raw_batch.columns[src_col_idx]));
                    }
                }

                result.total_rows += projected_batch.num_rows;
                result.batches.push_back(std::move(projected_batch));
            }
        }

        registry_.complete_select(plan.query_id, std::move(result));
    } catch (const std::exception& ex) {
        registry_.fail_query(plan.query_id, ex.what());
    }
}

} // namespace isbd