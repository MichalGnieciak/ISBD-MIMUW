#include "../io/deserializer.h"
#include <iostream>
#include <vector>
#include <cmath> // std::log2
#include <array>
#include <string>
#include <filesystem> 

int main(int argc, char* argv[]) {
    std::string filepath;

    // If an argument is provided, override the default
    if (argc >= 2) {
        filepath = argv[1];
        std::cout << "Using provided file: " << filepath << "\n";
    } 
    // Otherwise, fall back to the default project path
    else {
        filepath = "../test/resources/sample.isbd";
        std::cout << "No file provided. Defaulting to: " << filepath << "\n";
      
        if (!std::filesystem::exists(filepath)) {
            std::cerr << "\n[ERROR] Default file not found at: " << filepath << "\n";
            std::cerr << "Please ensure the file exists, or provide a custom path.\n";
            std::cerr << "Usage:   " << argv[0] << " [path_to_isbd_file]\n";
            return 1;
        }
    }

    try {
        // Pass the resolved filepath to the Deserializer
        Deserializer deserializer(filepath);
        const auto& schema = deserializer.get_schema();
        size_t num_columns = schema.size();

        // Accumulators for metrics
        std::vector<double> int_sums(num_columns, 0.0);
        std::vector<uint64_t> int_counts(num_columns, 0);
        
        std::vector<uint64_t> bool_ones(num_columns, 0);
        std::vector<uint64_t> bool_totals(num_columns, 0);
        
        std::vector<uint64_t> varchar_total_counts(num_columns, 0);
        std::vector<std::array<uint64_t, 256>> varchar_char_arrays(num_columns);

        std::cout << "Streaming data from database...\n";
        while (deserializer.has_next_batch()) {
            Batch batch = deserializer.read_next_batch();

            for (size_t i = 0; i < num_columns; ++i) {
                if (schema[i] == DataType::INT64) {
                    const auto& data = std::get<std::vector<int64_t>>(batch.columns[i]);
                    for (int64_t val : data) int_sums[i] += val;
                    int_counts[i] += data.size();
                } 
                else if (schema[i] == DataType::BOOL) {
                    const auto& data = std::get<std::vector<uint8_t>>(batch.columns[i]);
                    for (uint8_t val : data) bool_ones[i] += val;
                    bool_totals[i] += data.size();
                } 
                else if (schema[i] == DataType::VARCHAR) {
                    const auto& data = std::get<std::vector<std::string>>(batch.columns[i]);
                    for (const std::string& str : data) {
                        varchar_total_counts[i] += str.length();
                        for (char c : str) {
                            varchar_char_arrays[i][static_cast<unsigned char>(c)]++;
                        }
                    }
                }
            }
        }

        // Computing and printing metrics
        std::cout << "\n--- DATABASE METRICS ---\n";
        for (size_t i = 0; i < num_columns; ++i) {
            std::cout << "Column " << i << " ";
            
            if (schema[i] == DataType::INT64) {
                double mean = int_sums[i] / static_cast<double>(int_counts[i]);
                std::cout << "[INT64]: Mean = " << mean << "\n";
            } 
            else if (schema[i] == DataType::BOOL) {
                double p = static_cast<double>(bool_ones[i]) / bool_totals[i];
                double entropy = 0.0;
                if (p > 0.0 && p < 1.0) {
                    entropy = -p * std::log2(p) - (1.0 - p) * std::log2(1.0 - p);
                }
                std::cout << "[BOOL]: Binary Entropy = " << entropy << "\n";
            } 
            else if (schema[i] == DataType::VARCHAR) {
                std::cout << "[VARCHAR]: Total ASCII Characters = " << varchar_total_counts[i] << "\n";
                std::cout << "  Character Frequencies:\n";
                for (size_t ch = 0; ch < 256; ++ch) {
                    if (varchar_char_arrays[i][ch] > 0) {
                        if (ch >= 32 && ch <= 126) {
                            std::cout << "    '" << static_cast<char>(ch) << "': " << varchar_char_arrays[i][ch] << "\n";
                        } else {
                            std::cout << "    [ASCII " << ch << "]: " << varchar_char_arrays[i][ch] << "\n";
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
    }

    return 0;
}