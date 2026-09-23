#pragma once

#include "models/activity_record.h"

#include <cstdint>
#include <string>
#include <vector>

std::string serializePacket(const std::string& agentId, std::int64_t timestamp,
    const std::vector<ActivityRecord>& records);
