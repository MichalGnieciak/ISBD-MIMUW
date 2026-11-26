#pragma once
#include <cstdint>
#include <filesystem>
#include "format.h"
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace isbd {

std::string data_type_to_string(DataType type);
std::optional<DataType> string_to_data_type(const std::string& str);

struct ColumnSchema {
    std::string name;
    DataType type;
};

// RAII wrapper for physical .isbd files.
// Keeps files alive on disk as long as any active SELECT execution plan holds a shared_ptr,
// and automatically deletes the file when the last reference drops after a table deletion.
class ManagedFile {
public:
    explicit ManagedFile(std::filesystem::path path, bool delete_on_destroy = false)
        : path_(std::move(path)), delete_on_destroy_(delete_on_destroy) {}

    ~ManagedFile();

    ManagedFile(const ManagedFile&) = delete;
    ManagedFile& operator=(const ManagedFile&) = delete;

    const std::filesystem::path& path() const { return path_; }
    void mark_for_deletion() { delete_on_destroy_ = true; }

private:
    std::filesystem::path path_;
    bool delete_on_destroy_;
};

struct TableMetadata {
    std::string table_id;
    std::string name;
    std::vector<ColumnSchema> columns;
    std::vector<std::shared_ptr<ManagedFile>> files;
};

struct ShallowTableInfo {
    std::string table_id;
    std::string name;
};

struct ValidationProblem {
    std::string error;
    std::string context;
};

class Metastore {
public:
    explicit Metastore(const std::filesystem::path& data_dir);

    // Schema operations
    // On success returns table_id; on failure fills `problems` and returns std::nullopt.
    std::optional<std::string> create_table(
        const std::string& name,
        const std::vector<ColumnSchema>& columns,
        std::vector<ValidationProblem>& problems);

    bool delete_table(const std::string& table_id);

    std::vector<ShallowTableInfo> list_tables() const;
    std::optional<TableMetadata> get_table_by_id(const std::string& table_id) const;
    std::optional<TableMetadata> get_table_by_name(const std::string& name) const;

    // Atomically commits newly written .isbd files from a completed COPY query.
    // Returns false if the table was deleted while COPY was running (caller should clean up new_files).
    bool commit_copy_files(
        const std::string& table_id,
        const std::vector<std::filesystem::path>& new_file_paths);

    // Allocates a unique directory path for a table's data files
    std::filesystem::path table_data_dir(const std::string& table_id) const;

private:
    std::filesystem::path data_dir_;
    std::filesystem::path metastore_file_;
    std::filesystem::path temp_file_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, TableMetadata> tables_by_id_;
    std::unordered_map<std::string, std::string> name_to_id_;

    void load_from_disk();
    void persist_to_disk() const; // Caller must hold unique_lock on mutex_

    static uint32_t compute_crc32(const std::string& data);
};

} // namespace isbd