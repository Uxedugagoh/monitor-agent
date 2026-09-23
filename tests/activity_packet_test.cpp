#include "buffer/activity_queue.h"
#include "serialization/activity_packet.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>

void require(bool condition)
{
    if (!condition) throw std::runtime_error("Packet check failed");
}

int main()
{
    using namespace std::chrono_literals;
    ActivityRecord record;
    record.time = "2026-09-23 12:00:00";
    record.processName = "editor.exe";
    record.windowTitle = u8"\u0422\u0435\u0441\u0442 \U0001F680 \"quoted\" \\path\n\t";
    record.windowTitle.push_back('\0');
    record.userActive = true;
    const auto json = nlohmann::json::parse(serializePacket("TEST-PC", 1790000000, {record}));
    require(json.size() == 3 && json.at("agent_id") == "TEST-PC");
    require(json.at("timestamp").is_number_integer() && json.at("timestamp") == 1790000000);
    const auto& item = json.at("payload").at(0);
    require(item.size() == 4 && item.at("time") == record.time);
    require(item.at("process_name") == record.processName);
    require(item.at("window_title") == record.windowTitle);
    require(item.at("user_active").is_boolean() && item.at("user_active") == true);
    record.userActive = false;
    require(nlohmann::json::parse(serializePacket("PC", 0, {record}))["payload"][0]["user_active"] == false);
    require(nlohmann::json::parse(serializePacket("PC", 0, {}))["payload"].is_array());

    ActivityQueue threshold;
    for (int i = 0; i < 9; ++i) require(threshold.tryPush(record));
    auto reader = std::async(std::launch::async, [&] {
        std::vector<ActivityRecord> batch;
        require(threshold.waitBatch(batch, 5s));
        return batch.size();
    });
    require(reader.wait_for(50ms) == std::future_status::timeout);
    require(threshold.tryPush(record));
    require(reader.wait_for(2s) == std::future_status::ready);
    require(reader.get() == 10);

    ActivityQueue timed;
    require(timed.tryPush(record));
    std::vector<ActivityRecord> batch;
    const auto start = std::chrono::steady_clock::now();
    require(timed.waitBatch(batch, 100ms));
    require(std::chrono::steady_clock::now() - start >= 100ms);
    require(batch.size() == 1);
    require(timed.waitBatch(batch, 10ms) && batch.empty());

    for (int i = 0; i < 23; ++i) require(timed.tryPush(record));
    timed.close();
    for (const std::size_t expected : {10u, 10u, 3u})
    {
        require(timed.waitBatch(batch) && batch.size() == expected);
    }
    require(!timed.waitBatch(batch) && batch.empty());
    ActivityQueue closing;
    auto waiting = std::async(std::launch::async, [&] {
        std::vector<ActivityRecord> result;
        return closing.waitBatch(result);
    });
    closing.close();
    require(!waiting.get());
    std::cout << "PASS: JSON schema, Unicode/escaping, threshold, deadline, empty timeout, close/drain\n";
}
