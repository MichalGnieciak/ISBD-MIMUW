#include <fstream>
#include <iomanip>
#include <iostream>
#include "metastore.h"
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include "utils/uid.h"

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

using json = nlohmann::json;

namespace isbd {

// DataType Helpers

std::string data_type_to_string(DataType type) {
    switch (type) {
        case DataType::INT64:   return "INT64";
        case DataType::BOOL:    return "BOOL";
        case DataType::VARCHAR: return "VARCHAR";
    }
    return "UNKNOWN";
}

std::optional<DataType> string_to_data_type(const std::string& str) {
    if (str == "INT64") return DataType::INT64;
    if (str == "BOOL" || str == "BOOLEAN") return DataType::BOOL;
    if (str == "VARCHAR") return DataType::VARCHAR;
    return std::nullopt;
}

// ManagedFile Destructor

ManagedFile::~ManagedFile() {
    if (delete_on_destroy_) {
        std::error_code ec;
        std::filesystem::remove(path_, ec);

        // Attempt to remove parent directory if it is now empty
        if (path_.has_parent_path()) {
            std::filesystem::remove(path_.parent_path(), ec);
        }
    }
}

// Metastore Implementation

Metastore::Metastore(const std::filesystem::path& data_dir)
    : data_dir_(data_dir),
      metastore_file_(data_dir / "metastore.json"),
      temp_file_(data_dir / "metastore.json.tmp") {
    std::filesystem::create_directories(data_dir_);
    std::filesystem::create_directories(data_dir_ / "tables");
    load_from_disk();
}

std::filesystem::path Metastore::table_data_dir(const std::string& table_id) const {
    return data_dir_ / "tables" / table_id;
}

std::optional<std::string> Metastore::create_table(
    const std::string& name,
    const std::vector<ColumnSchema>& columns,
    std::vector<ValidationProblem>& problems) {

    std::unique_lock lock(mutex_);

    if (name.empty()) {
        problems.push_back({"Table name cannot be empty", "name"});
    }
    if (name_to_id_.count(name) > 0) {
        problems.push_back({"Table with name '" + name + "' already exists", "name"});
    }
    if (columns.empty()) {
        problems.push_back({"Table must contain at least one column", "columns"});
    }

    std::unordered_set<std::string> seen_cols;
    for (const auto& col : columns) {
        if (col.name.empty()) {
            problems.push_back({"Column name cannot be empty", "columns"});
        } else if (!seen_cols.insert(col.name).second) {
            problems.push_back({"Duplicate column name '" + col.name + "'", col.name});
        }
    }

    if (!problems.empty()) {
        return std::nullopt;
    }

    std::string table_id = generate_uid();
    std::filesystem::create_directories(table_data_dir(table_id));

    TableMetadata meta;
    meta.table_id = table_id;
    meta.name = name;
    meta.columns = columns;

    tables_by_id_[table_id] = meta;
    name_to_id_[name] = table_id;

    persist_to_disk();
    return table_id;
}

bool Metastore::delete_table(const std::string& table_id) {
    std::unique_lock lock(mutex_);

    auto it = tables_by_id_.find(table_id);
    if (it == tables_by_id_.end()) {
        return false;
    }

    // Mark all files for deletion. If no active SELECT holds a shared_ptr,
    // they delete immediately when it is erased; if a SELECT is running,
    // they delete as soon as that SELECT finishes
    for (auto& file_ptr : it->second.files) {
        file_ptr->mark_for_deletion();
    }

    name_to_id_.erase(it->second.name);
    tables_by_id_.erase(it);

    // Try removing the directory immediately if empty (no active readers)
    std::error_code ec;
    std::filesystem::remove(table_data_dir(table_id), ec);

    persist_to_disk();
    return true;
}

std::vector<ShallowTableInfo> Metastore::list_tables() const {
    std::shared_lock lock(mutex_);
    std::vector<ShallowTableInfo> result;
    result.reserve(tables_by_id_.size());
    for (const auto& [id, meta] : tables_by_id_) {
        result.push_back({meta.table_id, meta.name});
    }
    return result;
}

std::optional<TableMetadata> Metastore::get_table_by_id(const std::string& table_id) const {
    std::shared_lock lock(mutex_);
    auto it = tables_by_id_.find(table_id);
    if (it == tables_by_id_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<TableMetadata> Metastore::get_table_by_name(const std::string& name) const {
    std::shared_lock lock(mutex_);
    auto name_it = name_to_id_.find(name);
    if (name_it == name_to_id_.end()) {
        return std::nullopt;
    }
    return tables_by_id_.at(name_it->second);
}

bool Metastore::commit_copy_files(
    const std::string& table_id,
    const std::vector<std::filesystem::path>& new_file_paths) {

    std::unique_lock lock(mutex_);

    auto it = tables_by_id_.find(table_id);
    if (it == tables_by_id_.end()) {
        // Table was deleted while COPY was running
        return false;
    }

    for (const auto& p : new_file_paths) {
        it->second.files.push_back(std::make_shared<ManagedFile>(p, false));
    }

    persist_to_disk();
    return true;
}

// Crash-Safe Persistence (CRC32 + Temp File + Atomic Rename)
void Metastore::persist_to_disk() const {
    json tables_json = json::array();

    for (const auto& [id, meta] : tables_by_id_) {
        json cols_json = json::array();
        for (const auto& col : meta.columns) {
            cols_json.push_back({
                {"name", col.name},
                {"type", data_type_to_string(col.type)}
            });
        }

        json files_json = json::array();
        for (const auto& f : meta.files) {
            // Store relative path inside data_dir_ for portability across mounts
            std::error_code ec;
            auto rel = std::filesystem::relative(f->path(), data_dir_, ec);
            files_json.push_back(ec ? f->path().generic_string() : rel.generic_string());
        }

        tables_json.push_back({
            {"tableId", meta.table_id},
            {"name", meta.name},
            {"columns", cols_json},
            {"files", files_json}
        });
    }

    json payload = {{"tables", tables_json}};
    std::string serialized_payload = payload.dump(2);
    uint32_t checksum = compute_crc32(serialized_payload);

    json envelope = {
        {"checksum", checksum},
        {"payload", payload}
    };
    std::string final_bytes = envelope.dump(2);

    // Write to temporary file
    {
        std::ofstream out(temp_file_, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("Failed to open temporary metastore file for writing");
        }
        out.write(final_bytes.data(), static_cast<std::streamsize>(final_bytes.size()));
        out.flush();
        if (!out) {
            throw std::runtime_error("Failed while flushing temporary metastore file");
        }
    }

#ifndef _WIN32
    // Force OS page cache flush to physical disk
    int fd = ::open(temp_file_.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#endif

    // Atomic rename over the live metastore.json
    std::filesystem::rename(temp_file_, metastore_file_);

#ifndef _WIN32
    // Flush parent directory entry so the rename is durable
    int dir_fd = ::open(data_dir_.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }
#endif
}

void Metastore::load_from_disk() {
    std::unique_lock lock(mutex_);

    // Clean up any leftover .tmp file from an incomplete write before a crash
    if (std::filesystem::exists(temp_file_)) {
        std::error_code ec;
        std::filesystem::remove(temp_file_, ec);
    }

    // First-ever startup: initialize an empty metastore on disk
    if (!std::filesystem::exists(metastore_file_)) {
        persist_to_disk();
        return;
    }

    std::ifstream in(metastore_file_, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to open existing metastore.json");
    }

    json envelope = json::parse(in);
    if (!envelope.contains("checksum") || !envelope.contains("payload")) {
        throw std::runtime_error("Corrupted metastore.json: missing checksum or payload");
    }

    uint32_t expected_crc = envelope["checksum"].get<uint32_t>();
    std::string serialized_payload = envelope["payload"].dump(2);
    uint32_t actual_crc = compute_crc32(serialized_payload);

    if (expected_crc != actual_crc) {
        throw std::runtime_error("Corrupted metastore.json: CRC32 checksum mismatch!");
    }

    tables_by_id_.clear();
    name_to_id_.clear();

    for (const auto& t_json : envelope["payload"]["tables"]) {
        TableMetadata meta;
        meta.table_id = t_json.at("tableId").get<std::string>();
        meta.name = t_json.at("name").get<std::string>();

        for (const auto& c_json : t_json.at("columns")) {
            auto col_type = string_to_data_type(c_json.at("type").get<std::string>());
            if (!col_type) {
                throw std::runtime_error("Unknown column type in metastore.json");
            }
            meta.columns.push_back({c_json.at("name").get<std::string>(), *col_type});
        }

        for (const auto& f_json : t_json.at("files")) {
            std::filesystem::path rel_path = f_json.get<std::string>();
            std::filesystem::path full_path = rel_path.is_absolute() ? rel_path : (data_dir_ / rel_path);
            if (std::filesystem::exists(full_path)) {
                meta.files.push_back(std::make_shared<ManagedFile>(full_path, false));
            }
        }

        name_to_id_[meta.name] = meta.table_id;
        tables_by_id_[meta.table_id] = std::move(meta);
    }
}

uint32_t Metastore::compute_crc32(const std::string& data) {
    uint32_t crc = 0xFFFFFFFF;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

} // namespace isbd