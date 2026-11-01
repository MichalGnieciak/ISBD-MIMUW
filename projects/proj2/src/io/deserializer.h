#pragma once
#include "format.h"
#include "batch.h"
#include <string>
#include <vector>
#include <fstream>

class Deserializer {
public:
    // Opens the file
    Deserializer(const std::string& filepath);
  
    // Prevents accidental copying of file streams
    Deserializer(const Deserializer&) = delete;
    Deserializer& operator=(const Deserializer&) = delete;

    // Finilize reading the file
    ~Deserializer();

    // Streaming API
    bool has_next_batch() const;
    Batch read_next_batch();

    const std::vector<DataType>& get_schema() const { return m_schema; }
    uint64_t get_total_rows() const { return m_total_rows; }

private:
    std::ifstream m_in;
    // Schema of the database table
    std::vector<DataType> m_schema;
    
    uint64_t m_total_rows = 0;
    uint32_t m_num_row_groups = 0;
    // Progress tracking
    uint32_t m_current_row_group = 0;
};