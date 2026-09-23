#include "collector/activity_collector.h"
#include "buffer/activity_queue.h"
#include "network/http_sender.h"
#include "storage/backup_store.h"
#include "runtime/shutdown_monitor.h"

#include <windows.h>

#include <iostream>
#include <exception>
#include <atomic>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <mutex>

namespace
{
    std::atomic<bool> stopRequested{false};

    void collectPeriodically(ActivityQueue& queue, std::size_t& droppedRecords,
        const InputActivityMonitor& input)
    {
        using Clock = std::chrono::steady_clock;
        constexpr auto interval = std::chrono::seconds(5);
        constexpr auto stopCheckInterval = std::chrono::milliseconds(100);

        ActivityCollector collector(input);
        auto nextCollection = Clock::now();

        while (!stopRequested.load())
        {
            const auto now = Clock::now();
            if (now < nextCollection)
            {
                // Short sleeps keep Ctrl+C responsive without a busy loop.
                const auto nextCheck = now + stopCheckInterval;
                std::this_thread::sleep_until(
                    nextCheck < nextCollection ? nextCheck : nextCollection);
                continue;
            }

            if (!queue.tryPush(collector.collect()))
            {
                ++droppedRecords;
            }

            nextCollection += interval;
            // Skip missed intervals instead of producing a burst of samples.
            const auto finished = Clock::now();
            if (nextCollection <= finished)
            {
                nextCollection += interval * ((finished - nextCollection) / interval + 1);
            }
        }
    }

    std::string computerName()
    {
        wchar_t name[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD length = MAX_COMPUTERNAME_LENGTH + 1;
        if (!GetComputerNameW(name, &length))
        {
            throw std::runtime_error("Failed to obtain computer name");
        }
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            name, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
        if (size == 0) throw std::runtime_error("Failed to size computer name");
        std::string result(size, '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                name, static_cast<int>(length), result.data(), size, nullptr, nullptr))
        {
            throw std::runtime_error("Failed to convert computer name");
        }
        return result;
    }

}

int main()
{
    try
    {
        // Redirected output already contains UTF-8 bytes; only a console
        // needs its output code page configured.
        DWORD consoleMode = 0;
        if (GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &consoleMode))
        {
            if (!SetConsoleOutputCP(CP_UTF8))
            {
                std::cerr << "Failed to configure UTF-8 console output\n";
                return 1;
            }
        }

        const std::string agentId = computerName();
        // Load everything before starting collection. Invalid backups are never overwritten.
        BackupStore backup("backup.json");
        ActivityQueue queue;
        {
            const auto restored = backup.load();
            for (const auto& record : restored)
            {
                if (!queue.tryPush(record)) throw std::runtime_error("Cannot restore backup into queue");
            }
            std::cerr << "Restored " << restored.size() << " records from backup.json\n";
        }
        std::mutex checkpointMutex;
        const auto checkpoint = [&] {
            std::lock_guard<std::mutex> lock(checkpointMutex);
            backup.save(queue.snapshot());
        };
        ShutdownMonitor shutdown([&](bool urgent) noexcept {
            stopRequested.store(true);
            if (urgent)
            {
                // Freeze admission before saving. An in-flight unacknowledged
                // HTTP batch remains in the queue, so it is included as well.
                queue.close();
                std::lock_guard<std::mutex> lock(checkpointMutex);
                try
                {
                    backup.save(queue.snapshot());
                }
                catch (...)
                {
                    OutputDebugStringW(L"Monitor agent: emergency backup failed\n");
                }
            }
        });

        std::cerr << "Collecting every 5 seconds; POST to http://127.0.0.1:8080/ "
                     "every 30 seconds or 10 records. "
                     "Press Ctrl+C to stop.\n";
        std::exception_ptr workerError;
        std::size_t droppedRecords = 0;
        InputActivityMonitor input;
        std::thread worker([&]()
        {
            try
            {
                collectPeriodically(queue, droppedRecords, input);
            }
            catch (...)
            {
                workerError = std::current_exception();
                stopRequested.store(true);
            }
            // Wake the consumer on both normal completion and failure.
            queue.close();
        });

        try
        {
            sendPackets(queue, agentId);
        }
        catch (...)
        {
            stopRequested.store(true);
            worker.join();
            checkpoint();
            throw;
        }
        worker.join();
        checkpoint();
        std::cerr << "Saved unsent records to backup.json\n";
        if (droppedRecords > 0)
        {
            std::cerr << "Records skipped because the queue was full: "
                      << droppedRecords << '\n';
        }
        if (workerError)
        {
            std::rethrow_exception(workerError);
        }
        std::cerr << "Monitoring stopped.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Agent failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
