#include "storage/backup_store.h"
#include "buffer/activity_queue.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace
{
    constexpr std::size_t maxBackupBytes = 16 * 1024 * 1024;

    [[noreturn]] void fail(const char* message)
    {
        throw std::system_error(static_cast<int>(GetLastError()),
            std::system_category(), message);
    }
}

BackupStore::BackupStore(std::filesystem::path path) : path_(std::move(path))
{
    auto lockPath = path_;
    lockPath += L".lock";
    lockHandle_ = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lockHandle_ == INVALID_HANDLE_VALUE) fail("Cannot lock backup file");
}

BackupStore::~BackupStore()
{
    CloseHandle(lockHandle_);
}

std::vector<ActivityRecord> BackupStore::load() const
{
    if (!std::filesystem::exists(path_)) return {};
    if (std::filesystem::file_size(path_) > maxBackupBytes)
        throw std::runtime_error("Backup exceeds the 16 MiB limit; file preserved");

    std::ifstream input(path_, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open backup for reading");
    const auto json = nlohmann::json::parse(input);
    if (input.bad()) throw std::runtime_error("Error reading backup; file preserved");
    if (!json.is_object() || !json.contains("version") ||
        !json.at("version").is_number_integer() || json.at("version") != 1 ||
        !json.contains("records") || !json.at("records").is_array() ||
        json.at("records").size() > ActivityQueue::capacity)
        throw std::runtime_error("Invalid backup format or record count; file preserved");

    std::vector<ActivityRecord> result;
    for (const auto& item : json.at("records"))
    {
        if (!item.is_object() || !item.contains("time") || !item.at("time").is_string() ||
            !item.contains("process_name") || !item.at("process_name").is_string() ||
            !item.contains("window_title") || !item.at("window_title").is_string() ||
            !item.contains("user_active") || !item.at("user_active").is_boolean())
            throw std::runtime_error("Invalid backup record; file preserved");
        result.push_back({item.at("time").get<std::string>(),
            item.at("process_name").get<std::string>(),
            item.at("window_title").get<std::string>(),
            item.at("user_active").get<bool>()});
    }
    return result;
}

void BackupStore::save(const std::vector<ActivityRecord>& records) const
{
    if (records.size() > ActivityQueue::capacity)
        throw std::runtime_error("Too many records for backup");
    auto items = nlohmann::json::array();
    for (const auto& record : records)
        items.push_back({{"time", record.time}, {"process_name", record.processName},
            {"window_title", record.windowTitle}, {"user_active", record.userActive}});
    const auto body = nlohmann::json({{"version", 1}, {"records", std::move(items)}}).dump(2);
    if (body.size() > maxBackupBytes) throw std::runtime_error("Backup exceeds the 16 MiB limit");

    auto temporary = path_;
    temporary += L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) fail("Cannot create temporary backup");
    try
    {
        DWORD written = 0;
        if (!WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &written, nullptr))
            fail("Cannot write backup");
        if (written != body.size()) throw std::runtime_error("Incomplete backup write");
        if (!FlushFileBuffers(file)) fail("Cannot flush backup");
    }
    catch (...)
    {
        CloseHandle(file);
        throw;
    }
    if (!CloseHandle(file)) fail("Cannot close temporary backup");
    // Same-directory replacement: never truncate the previous valid backup.
    if (!MoveFileExW(temporary.c_str(), path_.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        fail("Cannot replace backup; previous file preserved");
}
