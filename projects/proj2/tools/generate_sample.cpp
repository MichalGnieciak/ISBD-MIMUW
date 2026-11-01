#include "../io/serializer.h"
#include "../io/batch.h"
#include "../utils/random_utils.h"
#include <iostream>
#include <vector>
#include <random>

// Verifiying temp file creation
#include <thread>
#include <chrono>

const size_t TOTAL_ROWS = 100000;
const size_t BATCH_SIZE = 8192;   // chosen to match L2 Cache & ZSTD

int main() {
    std::random_device rd;
    std::mt19937 rng(rd());

    // Task Schema: 1 INT64, 1 BOOL, 2 VARCHAR
    std::vector<DataType> schema = {
        DataType::INT64, DataType::BOOL, 
        DataType::VARCHAR, DataType::VARCHAR
    };

    // Creating the Serializer
    Serializer serializer("sample.isbd", schema);
    size_t rows_written = 0;

    while (rows_written < TOTAL_ROWS) {
        size_t current_batch_size = std::min(BATCH_SIZE, TOTAL_ROWS - rows_written);
        Batch batch;
        batch.num_rows = current_batch_size;
        
        // Pre-allocating vectors
        std::vector<int64_t> col0(current_batch_size);
        std::vector<uint8_t> col1(current_batch_size);
        std::vector<std::string> col2(current_batch_size);
        std::vector<std::string> col3(current_batch_size);

        // Populating the batch
        for (size_t i = 0; i < current_batch_size; ++i) {
            col0[i] = random_int64(rng, 1, 10000);
            col1[i] = random_bool(rng);
            col2[i] = random_string(rng, 15);
            col3[i] = random_string(rng, 20);
        }
        batch.columns.push_back(std::move(col0));
        batch.columns.push_back(std::move(col1));
        batch.columns.push_back(std::move(col2));
        batch.columns.push_back(std::move(col3));

        serializer.write_batch(batch);
        rows_written += current_batch_size;
        std::cout << "Wrote batch of " << current_batch_size << " rows...\n";
    }

    // Verifying the file generation
    std::this_thread::sleep_for(std::chrono::seconds(2));

    serializer.close();
    std::cout << "Successfully wrote " << TOTAL_ROWS << " rows to sample.mim\n";
    return 0;
}