#pragma once
#include <cstdint>

// Allowed data types
enum class DataType : uint8_t {
    INT64 = 0,
    VARCHAR = 1,
    BOOL = 2
};

// Force the compiler to pack these structs exactly as written
#pragma pack(push, 1)

// File Blueprint
// Global Header (byte offset 0)
struct GlobalHeader {
    char magic[4] = {'I', 'S', 'B', 'D'}; // Identifying the file format
    uint64_t num_rows;                    // Total number of rows (m)
    uint32_t num_columns;                 // Total number of columns (n)
    uint32_t num_row_groups;              // Total number of batches
    // Keeping the total number of batches allows for:
    // Silent Corruption Detection
    // Progress Tracking and Pre-allocation
};

// Column Metadata (immediately after the GlobalHeader, repeated for each column)
struct ColumnDef {
    DataType type;
};

// Batch Metadata (marks the beginning of a new Batch on disk)
struct RowGroupHeader {
    uint64_t num_rows; // BATCH_SIZE (BATCH_SIZE> for last batch)
};

// Chunk Metadata (repeated for each column after a RowGroupHeader)
// Tells the deserializer exactly how many bytes to read for this specific chunk.
struct ChunkMetadata {
    //              INT64   BOOL    VARCHAR
    // stream1_size bytes   bytes   index blob
    // stream2_size   0       0     text blob
    uint64_t stream1_size;
    uint64_t stream2_size; 
};

#pragma pack(pop)