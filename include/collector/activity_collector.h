#pragma once

#include "../models/activity_record.h"
#include "collector/input_activity.h"

class ActivityCollector
{
public:
    explicit ActivityCollector(const InputActivityMonitor& input) : input_(input) {}
    ActivityRecord collect();
private:
    const InputActivityMonitor& input_;
};
