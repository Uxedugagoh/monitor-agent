#pragma once

#include "models/activity_record.h"
#include <filesystem>
#include <vector>

// One process owns a backup path at a time. Keep this object alive until exit.
class BackupStore
{
public:
    explicit BackupStore(std::filesystem::path path);
    ~BackupStore();
    BackupStore(const BackupStore&) = delete;
    BackupStore& operator=(const BackupStore&) = delete;

    std::vector<ActivityRecord> load() const;
    void save(const std::vector<ActivityRecord>& records) const;

private:
    std::filesystem::path path_;
    void* lockHandle_ = nullptr;
};
