#include "runtime/shutdown_monitor.h"
#include "storage/backup_store.h"
#include "buffer/activity_queue.h"
#include <windows.h>
#include <atomic>
#include <iostream>
#include <mutex>
#include <stdexcept>

void require(bool condition)
{
    if (!condition) throw std::runtime_error("Shutdown monitor check failed");
}

int main()
{
    namespace fs = std::filesystem;
    const auto path = fs::current_path() / ("shutdown-test-" + std::to_string(GetCurrentProcessId()) + ".json");
    {
        BackupStore backup(path);
        ActivityQueue queue;
        require(queue.tryPush({"synthetic", "test.exe", "Pending HTTP batch", false}));
        std::atomic<int> calls{0};
        std::atomic<bool> failed{false};
        {
            ShutdownMonitor monitor([&](bool urgent) noexcept {
                if (!urgent) { failed.store(true); return; }
                queue.close();
                try { backup.save(queue.snapshot()); }
                catch (...) { failed.store(true); }
                ++calls;
            });
            HWND window = FindWindowW(L"MonitorAgentShutdownWindow", L"MonitorAgentShutdown");
            require(window != nullptr && !IsWindowVisible(window));
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            require(process == GetCurrentProcessId());
            require(SendMessageW(window, WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF) == TRUE);
            require(calls.load() == 0);
            SendMessageW(window, WM_ENDSESSION, FALSE, ENDSESSION_LOGOFF);
            require(calls.load() == 0 && queue.tryPush({"second", "", "", false}));
            const auto start = std::chrono::steady_clock::now();
            SendMessageW(window, WM_ENDSESSION, TRUE, ENDSESSION_LOGOFF);
            require(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
            require(calls.load() == 1 && !failed.load());
            require(!queue.tryPush({}));
            require(backup.load().size() == 2);
            // Repeated confirmed notification remains safe.
            SendMessageW(window, WM_ENDSESSION, TRUE, 0);
            require(calls.load() == 2 && !failed.load() && backup.load().size() == 2);
        }
        // Window class and console registration are reusable after destruction.
        { ShutdownMonitor again([](bool) noexcept {}); }
    }
    fs::remove(path);
    auto lock = path; lock += L".lock"; fs::remove(lock);
    std::cout << "PASS: hidden window, query/cancel, confirmed logoff/shutdown, backup before return, teardown\n";
}
