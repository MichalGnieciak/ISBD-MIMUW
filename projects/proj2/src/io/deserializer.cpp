#include "deserializer.h"
#include "../compression/decoding.h"
#include <stdexcept>

Deserializer::Deserializer(const std::string& filepath) {
    m_in.open(filepath, std::ios::binary);
    if (!m_in) throw std::runtime_error("Failed to open file: " + filepath);

    GlobalHeader header;
    m_in.read(reinterpret_cast<char*>(&header), sizeof(GlobalHeader));

    // Verify magic bytes (is the file .isbd)
    if (header.magic[0] != 'I' || header.magic[1] != 'S' || 
        header.magic[2] != 'B' || header.magic[3] != 'D') {
        throw std::runtime_error("Invalid file format: must be .isbd");
    }

    m_total_rows = header.num_rows;
    m_num_row_groups = header.num_row_groups;

    // Read Schema
    for (uint32_t i = 0; i < header.num_columns; ++i) {
        ColumnDef def;
        m_in.read(reinterpret_cast<char*>(&def), sizeof(ColumnDef));
        m_schema.push_back(def.type);
    }
}

bool Deserializer::has_next_batch() const {
    return m_current_row_group < m_num_row_groups;
}

Batch Deserializer::read_next_batch() {
    if (!has_next_batch()) throw std::runtime_error("No more batches to read.");

    Batch batch;
    RowGroupHeader rg_header;
    m_in.read(reinterpret_cast<char*>(&rg_header), sizeof(RowGroupHeader));
    batch.num_rows = rg_header.num_rows;

    std::vector<ChunkMetadata> chunk_meta(m_schema.size());
    for (size_t i = 0; i < m_schema.size(); ++i) {
        m_in.read(reinterpret_cast<char*>(&chunk_meta[i]), sizeof(ChunkMetadata));
    }

    // Read and decode each column
    for (size_t i = 0; i < m_schema.size(); ++i) {
        std::vector<uint8_t> s1(chunk_meta[i].stream1_size);
        std::vector<uint8_t> s2(chunk_meta[i].stream2_size);

        if (!s1.empty()) m_in.read(reinterpret_cast<char*>(s1.data()), s1.size());
        if (!s2.empty()) m_in.read(reinterpret_cast<char*>(s2.data()), s2.size());

        if (m_schema[i] == DataType::INT64) {
            batch.columns.push_back(decode_int64_column(s1));
        } 
        else if (m_schema[i] == DataType::BOOL) {
            batch.columns.push_back(decode_bool_column(s1, batch.num_rows));
        } 
        else if (m_schema[i] == DataType::VARCHAR) {
            batch.columns.push_back(decode_varchar_column(s1, s2));
        }
    }

    m_current_row_group++;
    return batch;
}

Deserializer::~Deserializer() {
    if (m_in.is_open()) m_in.close();
}