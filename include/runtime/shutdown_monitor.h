#pragma once

#include <functional>
#include <memory>

// Calls request(false) for Ctrl+C/Break and request(true) for forced session
// completion/console close. The callback must not throw and must be thread-safe.
class ShutdownMonitor
{
public:
    explicit ShutdownMonitor(std::function<void(bool)> request);
    ~ShutdownMonitor();
    ShutdownMonitor(const ShutdownMonitor&) = delete;
    ShutdownMonitor& operator=(const ShutdownMonitor&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
