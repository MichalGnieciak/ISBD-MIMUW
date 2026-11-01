#include "../io/serializer.h"
#include "../io/deserializer.h"
#include "../utils/random_utils.h"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <random>

const size_t TOTAL_ROWS = 20000;
const size_t BATCH_SIZE = 8192;
const size_t SEED = 42;

int main() {
    const std::string test_file = "e2e_random_test.isbd";
    std::mt19937 rng(SEED);
    
    std::vector<DataType> schema = { DataType::INT64, DataType::BOOL, DataType::VARCHAR };

    size_t total_rows_to_test = TOTAL_ROWS;

    std::cout << "1. Generating " << total_rows_to_test << " random rows...\n";
    std::vector<int64_t> original_int(total_rows_to_test);
    std::vector<uint8_t> original_bool(total_rows_to_test);
    std::vector<std::string> original_str(total_rows_to_test);

    for (size_t i = 0; i < total_rows_to_test; ++i) {
        original_int[i] = random_int64(rng, -10000, 10000);
        original_bool[i] = random_bool(rng);
        original_str[i] = random_string(rng, 20);
    }

    std::cout << "2. Serializing to disk...\n";
    {
        Serializer serializer(test_file, schema);
        size_t written = 0;
        while (written < total_rows_to_test) {
            size_t batch_size = std::min(static_cast<size_t>(BATCH_SIZE), total_rows_to_test - written);
            Batch b;
            b.num_rows = batch_size;
            b.columns.push_back(std::vector<int64_t>(original_int.begin() + written, original_int.begin() + written + batch_size));
            b.columns.push_back(std::vector<uint8_t>(original_bool.begin() + written, original_bool.begin() + written + batch_size));
            b.columns.push_back(std::vector<std::string>(original_str.begin() + written, original_str.begin() + written + batch_size));
            serializer.write_batch(b);
            written += batch_size;
        }
        serializer.close();
    }

    std::cout << "3. Deserializing and Verifying...\n";
    {
        Deserializer deserializer(test_file);
        assert(deserializer.get_schema() == schema);
        assert(deserializer.get_total_rows() == total_rows_to_test);
        
        size_t read_rows = 0;
        while (deserializer.has_next_batch()) {
            Batch b = deserializer.read_next_batch();
            
            const auto& loaded_int = std::get<std::vector<int64_t>>(b.columns[0]);
            const auto& loaded_bool = std::get<std::vector<uint8_t>>(b.columns[1]);
            const auto& loaded_str = std::get<std::vector<std::string>>(b.columns[2]);

            for (size_t i = 0; i < b.num_rows; ++i) {
                assert(loaded_int[i] == original_int[read_rows + i]);
                assert(loaded_bool[i] == original_bool[read_rows + i]);
                assert(loaded_str[i] == original_str[read_rows + i]);
            }
            read_rows += b.num_rows;
        }
        assert(read_rows == total_rows_to_test);
    }

    std::filesystem::remove(test_file);
    std::cout << "SUCCESS: End-to-End Randomized Test Passed!\n";
    return 0;
}