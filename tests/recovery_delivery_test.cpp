#include "network/http_sender.h"
#include "storage/backup_store.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>

void require(bool value)
{
    if (!value) throw std::runtime_error("Recovery/delivery check failed");
}

int main()
{
    using namespace std::chrono_literals;
    namespace fs = std::filesystem;
    const auto path = fs::current_path() /
        ("recovery-delivery-" + std::to_string(GetCurrentProcessId()) + ".json");
    httplib::Server server;
    std::atomic<bool> accepting{false};
    std::atomic<int> attempts{0};
    std::vector<std::string> accepted;
    server.Post("/", [&](const httplib::Request& req, httplib::Response& res) {
        ++attempts;
        if (!accepting.load()) { res.status = 503; return; }
        const auto packet = nlohmann::json::parse(req.body);
        for (const auto& item : packet.at("payload"))
            accepted.push_back(item.at("time").get<std::string>());
        res.status = 204;
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    require(port > 0);
    auto serving = std::async(std::launch::async, [&] { return server.listen_after_bind(); });
    try
    {
        while (!server.is_running()) std::this_thread::sleep_for(1ms);
        {
            BackupStore store(path);
            ActivityQueue original;
            for (int i = 0; i < 13; ++i)
                require(original.tryPush({std::to_string(i), "test.exe", "Synthetic", false}));
            auto sender = std::async(std::launch::async, [&] {
                sendPackets(original, "TEST", port, 10ms, 10ms);
            });
            const auto deadline = std::chrono::steady_clock::now() + 3s;
            while (attempts.load() == 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(1ms);
            original.close();
            sender.get();
            require(attempts.load() > 0 && original.snapshot().size() == 13);
            store.save(original.snapshot());
        }
        accepting.store(true);
        {
            BackupStore reopened(path);
            ActivityQueue restored;
            for (const auto& record : reopened.load()) require(restored.tryPush(record));
            auto sender = std::async(std::launch::async, [&] {
                sendPackets(restored, "TEST", port, 10ms, 10ms);
            });
            const auto deadline = std::chrono::steady_clock::now() + 3s;
            while (!restored.snapshot().empty() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(1ms);
            restored.close();
            sender.get();
            require(restored.snapshot().empty());
            reopened.save(restored.snapshot());
        }
        { BackupStore finalStore(path); require(finalStore.load().empty()); }
    }
    catch (...)
    {
        server.stop();
        serving.wait();
        throw;
    }
    server.stop();
    require(serving.get());
    require(accepted.size() == 13);
    for (int i = 0; i < 13; ++i) require(accepted[i] == std::to_string(i));
    fs::remove(path);
    auto lock = path; lock += L".lock"; fs::remove(lock);
    std::cout << "PASS: failed POST -> backup -> reopen -> delivery in order -> empty backup\n";
}
