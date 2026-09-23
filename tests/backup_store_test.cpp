#include "storage/backup_store.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

void require(bool condition)
{
    if (!condition) throw std::runtime_error("Backup check failed");
}

template<class Operation> void rejects(Operation operation)
{
    bool rejected = false;
    try { operation(); }
    catch (const std::exception&) { rejected = true; }
    require(rejected);
}

int main()
{
    namespace fs = std::filesystem;
    const auto dir = fs::current_path() / ("backup-test-" + std::to_string(GetCurrentProcessId()));
    require(fs::create_directory(dir));
    const auto path = dir / L"\u0440\u0435\u0437\u0435\u0440\u0432.json";
    ActivityRecord record{"2026-09-23 12:00:00", "editor.exe",
        u8"\u0422\u0435\u0441\u0442 \U0001F680 \"quoted\"\n", true};
    {
        BackupStore store(path);
        require(store.load().empty());
        rejects([&] { BackupStore second(path); });
        store.save({record});
        const auto loaded = store.load();
        require(loaded.size() == 1 && loaded[0].time == record.time &&
            loaded[0].processName == record.processName &&
            loaded[0].windowTitle == record.windowTitle && loaded[0].userActive);
        // Deny replacement of the destination, but allow the temporary write.
        HANDLE blocked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(blocked != INVALID_HANDLE_VALUE);
        bool failed = false;
        try { store.save({}); } catch (const std::exception&) { failed = true; }
        CloseHandle(blocked);
        require(failed && store.load().size() == 1);
        store.save(std::vector<ActivityRecord>(100, record));
        require(store.load().size() == 100);
        rejects([&] { store.save(std::vector<ActivityRecord>(101, record)); });
        require(store.load().size() == 100);
        store.save({});
        require(store.load().empty());
        for (const auto* broken : {"{", "", "{\"version\":2,\"records\":[]}",
            "{\"version\":1,\"records\":[{\"time\":12}]}",
            "{\"version\":1,\"records\":null}"})
        {
            { std::ofstream file(path, std::ios::binary); file << broken; }
            rejects([&] { store.load(); });
            std::ifstream file(path, std::ios::binary);
            const std::string unchanged((std::istreambuf_iterator<char>(file)), {});
            require(unchanged == broken);
        }
        { // Oversized logical queue must not be truncated during recovery.
            std::ofstream file(path);
            file << "{\"version\":1,\"records\":[";
            for (int i = 0; i < 101; ++i) file << (i ? ",{}" : "{}");
            file << "]}";
        }
        rejects([&] { store.load(); });
        store.save({record});
    }
    { // Closing/reopening a store simulates ownership across restarts.
        BackupStore reopened(path);
        require(reopened.load().size() == 1);
    }
    rejects([&] { BackupStore missingParent(dir / "missing" / "backup.json"); });
    // Only individual files in the test-created directory are removed.
    fs::remove(path);
    auto lock = path; lock += L".lock"; fs::remove(lock);
    auto tmp = path; tmp += L".tmp"; fs::remove(tmp);
    fs::remove(dir);
    std::cout << "PASS: restore, Unicode, exclusive ownership, replacement failure, empty/100 records, invalid input\n";
}
