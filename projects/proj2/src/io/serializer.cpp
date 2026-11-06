#include "serializer.h"
#include "../compression/encoding.h"
#include <stdexcept>
#include <iostream>

Serializer::Serializer(const std::string& filepath, const std::vector<DataType>& schema) 
    : m_schema(schema),
      m_final_path(filepath), 
      m_tmp_path(filepath + ".tmp") {
    
    m_out.open(m_tmp_path, std::ios::binary);
    if (!m_out) {
        throw std::runtime_error("Failed to open temp file for writing: " + m_tmp_path);
    }

    // dummy GlobalHeader (overwritten in close)
    GlobalHeader dummy_header;
    m_out.write(reinterpret_cast<const char*>(&dummy_header), sizeof(GlobalHeader));

    // ColumnDefs
    for (DataType type : m_schema) {
        ColumnDef def{type};
        m_out.write(reinterpret_cast<const char*>(&def), sizeof(ColumnDef));
    }
}

void Serializer::write_batch(const Batch& batch) {
    if (m_closed) throw std::runtime_error("Cannot write to a closed serializer.");
    if (batch.columns.size() != m_schema.size()) {
        throw std::runtime_error("Batch column count does not match schema.");
    }

    // RowGroupHeader
    RowGroupHeader rg_header{batch.num_rows};
    m_out.write(reinterpret_cast<const char*>(&rg_header), sizeof(RowGroupHeader));

    // Temporary storage for this specific chunk's compressed bytes and metadata
    std::vector<ChunkMetadata> chunk_meta;
    std::vector<std::vector<uint8_t>> stream1_buffers;
    std::vector<std::vector<uint8_t>> stream2_buffers;

    // Compress each column in the batch
    for (size_t i = 0; i < m_schema.size(); ++i) {
        ChunkMetadata meta{0, 0};
        std::vector<uint8_t> s1, s2;

        if (m_schema[i] == DataType::INT64) {
            const auto& data = std::get<std::vector<int64_t>>(batch.columns[i]);
            encode_int64_column(data, s1);
            meta.stream1_size = s1.size();
        } 
        else if (m_schema[i] == DataType::BOOL) {
            const auto& data = std::get<std::vector<uint8_t>>(batch.columns[i]);
            encode_bool_column(data, s1);
            meta.stream1_size = s1.size();
        } 
        else if (m_schema[i] == DataType::VARCHAR) {
            const auto& data = std::get<std::vector<std::string>>(batch.columns[i]);
            encode_varchar_column(data, s1, s2);
            meta.stream1_size = s1.size();
            meta.stream2_size = s2.size();
        }

        chunk_meta.push_back(meta);
        stream1_buffers.push_back(std::move(s1));
        stream2_buffers.push_back(std::move(s2));
    }

    // ChunkMetadata
    for (const auto& meta : chunk_meta) {
        m_out.write(reinterpret_cast<const char*>(&meta), sizeof(ChunkMetadata));
    }

    // Write actual compressed bytes for this RowGroup
    for (size_t i = 0; i < m_schema.size(); ++i) {
        if (!stream1_buffers[i].empty()) {
            m_out.write(reinterpret_cast<const char*>(stream1_buffers[i].data()), stream1_buffers[i].size());
        }
        if (!stream2_buffers[i].empty()) {
            m_out.write(reinterpret_cast<const char*>(stream2_buffers[i].data()), stream2_buffers[i].size());
        }
    }

    m_total_rows += batch.num_rows;
    m_num_row_groups += 1;
}

void Serializer::close() {
    if (m_closed) return;

    // Overriting header at the beggining
    m_out.seekp(0, std::ios::beg);
    GlobalHeader final_header;
    final_header.magic[0] = 'I'; 
    final_header.magic[1] = 'S'; 
    final_header.magic[2] = 'B'; 
    final_header.magic[3] = 'D';
    final_header.num_rows = m_total_rows;
    final_header.num_columns = static_cast<uint32_t>(m_schema.size());
    final_header.num_row_groups = m_num_row_groups;
    
    m_out.write(reinterpret_cast<const char*>(&final_header), sizeof(GlobalHeader));
    m_out.close();

    // Atomically rename the .tmp file to the final .isbd file
    std::filesystem::rename(m_tmp_path, m_final_path);
    
    m_closed = true;
}

Serializer::~Serializer() {
    if (!m_closed) {
        // If close wasn't succesfully executed
        // the .tmp is corrupted, remove
        m_out.close();
        std::error_code ec;
        std::filesystem::remove(m_tmp_path, ec);
    }
}