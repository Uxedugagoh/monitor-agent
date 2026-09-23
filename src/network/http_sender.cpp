#include "network/http_sender.h"
#include "serialization/activity_packet.h"
#include <httplib.h>
#include <iostream>

HttpResult postPacket(const std::string& body, int port)
{
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(2);
    client.set_read_timeout(2);
    client.set_write_timeout(2);
    // Redirects are not acknowledgements and must not forward user data.
    client.set_follow_location(false);
    auto response = client.Post("/", body, "application/json");
    if (!response) return {false, httplib::to_string(response.error())};
    return {response->status >= 200 && response->status < 300,
        "HTTP " + std::to_string(response->status)};
}

void sendPackets(ActivityQueue& queue, const std::string& agentId, int port,
    std::chrono::milliseconds batchInterval, std::chrono::milliseconds retryInterval)
{
    std::vector<ActivityRecord> batch;
    while (queue.waitPeekBatch(batch, batchInterval))
    {
        if (batch.empty()) continue;
        const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        // Keep the same timestamp and payload on all attempts for this batch.
        const auto body = serializePacket(agentId, timestamp, batch);
        while (!queue.waitClosed(std::chrono::milliseconds(0)))
        {
            const auto result = postPacket(body, port);
            if (result.success)
            {
                queue.acknowledge(batch.size());
                std::cerr << "Sent " << batch.size() << " records: " << result.description << '\n';
                break;
            }
            std::cerr << "POST failed: " << result.description << "; keeping records for retry\n";
            if (queue.waitClosed(retryInterval)) return;
        }
    }
}
