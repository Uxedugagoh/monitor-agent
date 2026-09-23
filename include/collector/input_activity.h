#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

// Stores only a timestamp, never keys, characters or mouse coordinates.
class InputActivityState
{
public:
    void keyboardEvent(std::uint32_t flags, std::int64_t timeMs) noexcept;
    void mouseEvent(std::uint32_t flags, std::int64_t timeMs) noexcept;
    bool activeAt(std::int64_t timeMs) const noexcept;

private:
    std::atomic<std::int64_t> lastInputMs_{-1};
};

class InputActivityMonitor
{
public:
    InputActivityMonitor();
    ~InputActivityMonitor();
    InputActivityMonitor(const InputActivityMonitor&) = delete;
    InputActivityMonitor& operator=(const InputActivityMonitor&) = delete;
    bool hasRecentInput() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
