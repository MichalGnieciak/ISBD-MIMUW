#include "batch.h"

Batch Batch::create_empty(const std::vector<DataType>& schema, size_t reserve_rows) {
    Batch batch;
    batch.num_rows = 0;
    batch.columns.reserve(schema.size());

    for (DataType dt : schema) {
        switch (dt) {
            case DataType::INT64: {
                std::vector<int64_t> col;
                if (reserve_rows > 0) col.reserve(reserve_rows);
                batch.columns.emplace_back(std::move(col));
                break;
            }
            case DataType::VARCHAR: {
                std::vector<std::string> col;
                if (reserve_rows > 0) col.reserve(reserve_rows);
                batch.columns.emplace_back(std::move(col));
                break;
            }
            case DataType::BOOL: {
                std::vector<uint8_t> col;
                if (reserve_rows > 0) col.reserve(reserve_rows);
                batch.columns.emplace_back(std::move(col));
                break;
            }
        }
    }
    return batch;
}