#include "network/http_sender.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

void require(bool condition)
{
    if (!condition) throw std::runtime_error("HTTP check failed");
}

int main()
{
    using namespace std::chrono_literals;
    httplib::Server server;
    std::atomic<int> status{500};
    std::mutex bodiesMutex;
    std::vector<std::string> bodies;
    server.Post("/", [&](const httplib::Request& request, httplib::Response& response) {
        if (request.get_header_value("Content-Type") != "application/json")
        {
            response.status = 415;
            return;
        }
        {
            std::lock_guard<std::mutex> lock(bodiesMutex);
            bodies.push_back(request.body);
        }
        response.status = status.load();
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    require(port > 0);
    // Listening socket without a request loop: verifies the read timeout.
    require(!postPacket("{}", port).success);
    auto serving = std::async(std::launch::async, [&] { return server.listen_after_bind(); });
    ActivityQueue queue;
    std::future<void> sending;
    try
    {
        while (!server.is_running()) std::this_thread::sleep_for(1ms);
        require(!postPacket("{}", port).success);
        status.store(400);
        require(!postPacket("{}", port).success);
        status.store(204);
        require(postPacket("{}", port).success);
        {
            std::lock_guard<std::mutex> lock(bodiesMutex);
            bodies.clear();
        }
        status.store(500);
        for (int i = 0; i < 10; ++i)
        {
            ActivityRecord record;
            record.time = std::to_string(i);
            record.windowTitle = u8"\u0422\u0435\u0441\u0442 \"JSON\"";
            require(queue.tryPush(record));
        }
        sending = std::async(std::launch::async, [&] { sendPackets(queue, "TEST", port, 50ms, 50ms); });
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        for (;;)
        {
            {
                std::lock_guard<std::mutex> lock(bodiesMutex);
                if (bodies.size() >= 2) break;
            }
            require(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(5ms);
        }
        require(queue.snapshot().size() == 10);
        for (int i = 10; i < 100; ++i)
        {
            ActivityRecord record;
            record.time = std::to_string(i);
            require(queue.tryPush(record));
        }
        require(!queue.tryPush({}));
        require(queue.snapshot().size() == 100);
        {
            std::lock_guard<std::mutex> lock(bodiesMutex);
            require(bodies[0] == bodies[1]);
            const auto packet = nlohmann::json::parse(bodies[0]);
            require(packet["payload"].size() == 10 && packet["agent_id"] == "TEST");
        }
        status.store(204);
        const auto recoveredDeadline = std::chrono::steady_clock::now() + 5s;
        while (!queue.snapshot().empty())
        {
            require(std::chrono::steady_clock::now() < recoveredDeadline);
            std::this_thread::sleep_for(5ms);
        }
        queue.close();
        sending.get();
        // A failed packet must survive shutdown as well.
        ActivityQueue stopping;
        require(stopping.tryPush({}));
        status.store(503);
        auto stoppedSender = std::async(std::launch::async, [&] {
            sendPackets(stopping, "TEST", port, 1ms, 5s);
        });
        std::this_thread::sleep_for(100ms);
        stopping.close();
        require(stoppedSender.wait_for(1s) == std::future_status::ready);
        stoppedSender.get();
        require(stopping.snapshot().size() == 1);
    }
    catch (...)
    {
        queue.close();
        if (sending.valid()) sending.wait();
        server.stop();
        serving.wait();
        throw;
    }
    server.stop();
    require(serving.get());
    require(!postPacket("{}", port).success);
    std::cout << "PASS: POST, 2xx/4xx/5xx, read timeout, connection failure, retries, capacity, recovery, shutdown retention\n";
}
