#include "serialization/activity_packet.h"

#include <nlohmann/json.hpp>

std::string serializePacket(const std::string& agentId, std::int64_t timestamp,
    const std::vector<ActivityRecord>& records)
{
    auto payload = nlohmann::json::array();
    for (const auto& record : records)
    {
        payload.push_back({
            {"time", record.time},
            {"process_name", record.processName},
            {"window_title", record.windowTitle},
            {"user_active", record.userActive}
        });
    }
    const nlohmann::json packet = {
        {"agent_id", agentId},
        {"timestamp", timestamp},
        {"payload", std::move(payload)}
    };
    return packet.dump(2);
}
