#pragma once
#include "format.h"
#include "batch.h"
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

class Serializer {
public:
    // Opens the file and writes the initial schema headers
    Serializer(const std::string& filepath, const std::vector<DataType>& schema);
    
    // Prevents accidental copying of file streams
    Serializer(const Serializer&) = delete;
    Serializer& operator=(const Serializer&) = delete;

    // Finalizes the file (updates the global header with actual counts)
    ~Serializer();
    
    void close();
    // Compresses a single Batch and flushes it immediately to disk
    void write_batch(const Batch& batch);

private:
    std::ofstream m_out;
    // Schema of the database table
    std::vector<DataType> m_schema;

    std::string m_final_path;
    std::string m_tmp_path;
    
    uint64_t m_total_rows = 0;
    uint32_t m_num_row_groups = 0;
    bool m_closed = false;
};