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
};

// Column Metadata (immediately after the GlobalHeader, repeated for each column)
struct ColumnMetadata {
    DataType type;           // The data type of this column
    uint64_t byte_offset;    // The absolute byte position in the file where this column's data begins

    //              INT64   BOOL    VARCHAR
    // stream1_size bytes   bytes   index blob
    // stream2_size   0       0     text blob
    uint64_t stream1_size;  
    uint64_t stream2_size;
};

#pragma pack(pop)