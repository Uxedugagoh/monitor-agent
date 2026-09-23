#pragma once

#include "buffer/activity_queue.h"
#include <string>

struct HttpResult
{
    bool success;
    std::string description;
};

HttpResult postPacket(const std::string& body, int port = 8080);
void sendPackets(ActivityQueue& queue, const std::string& agentId, int port = 8080,
    std::chrono::milliseconds batchInterval = std::chrono::seconds(30),
    std::chrono::milliseconds retryInterval = std::chrono::seconds(5));
