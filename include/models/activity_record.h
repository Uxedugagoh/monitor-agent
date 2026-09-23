#pragma once

#include <string>

struct ActivityRecord
{
    std::string time;
    // Text collected from Windows is stored as UTF-8.
    std::string processName;
    std::string windowTitle;
    bool userActive = false;
};
