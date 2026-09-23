// Exercise the real startup/shutdown path without collecting user activity.
#define main agentMain
#include "../src/main.cpp"
#undef main

#include <fstream>

int main()
{
    namespace fs = std::filesystem;
    const auto original = fs::current_path();
    const auto dir = original / ("lifecycle-test-" + std::to_string(GetCurrentProcessId()));
    if (!fs::create_directory(dir)) return 1;
    fs::current_path(dir);
    {
        BackupStore store("backup.json");
        store.save({{"original", "test.exe", "Synthetic test record", false}});
    }
    // Stop is already requested: startup must restore, then save the same record.
    stopRequested.store(true);
    if (agentMain() != 0) return 2;
    if (agentMain() != 0) return 3;
    {
        BackupStore store("backup.json");
        const auto records = store.load();
        if (records.size() != 1 || records[0].time != "original") return 4;
    }
    { std::ofstream file("backup.json"); file << "broken"; }
    if (agentMain() == 0) return 5;
    {
        std::ifstream file("backup.json");
        std::string value; file >> value;
        if (value != "broken") return 6;
    }
    fs::remove("backup.json");
    fs::remove("backup.json.lock");
    fs::current_path(original);
    fs::remove(dir);
    std::cout << "PASS: real startup/stop/restart and corrupt-file preservation\n";
}
