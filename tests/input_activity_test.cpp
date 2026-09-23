#include "collector/input_activity.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <thread>

void require(bool condition)
{
    if (!condition) throw std::runtime_error("Input activity check failed");
}

int main()
{
    InputActivityState state;
    require(!state.activeAt(10000));
    state.keyboardEvent(LLKHF_INJECTED, 10000);
    state.keyboardEvent(LLKHF_LOWER_IL_INJECTED, 10000);
    state.mouseEvent(LLMHF_INJECTED, 10000);
    state.mouseEvent(LLMHF_LOWER_IL_INJECTED, 10000);
    require(!state.activeAt(10000));

    state.keyboardEvent(LLKHF_EXTENDED, 10000);
    require(!state.activeAt(9999));
    require(state.activeAt(10000));
    require(state.activeAt(14999));
    require(!state.activeAt(15000));
    state.keyboardEvent(LLKHF_INJECTED | LLKHF_UP, 14999);
    state.mouseEvent(LLMHF_INJECTED, 14999);
    require(!state.activeAt(15000));
    state.mouseEvent(0, 15000);
    require(state.activeAt(19999));
    require(!state.activeAt(20000));
    state.keyboardEvent(LLKHF_UP | LLKHF_ALTDOWN, 20000);
    require(state.activeAt(20000));

    // TickCount64 continues beyond the old 32-bit wrap boundary.
    constexpr std::int64_t afterWrap = (std::int64_t{1} << 32) + 100;
    state.mouseEvent(0, afterWrap);
    require(state.activeAt(afterWrap + 4999));
    require(!state.activeAt(afterWrap + 5000));

    InputActivityState concurrent;
    std::thread writer([&] {
        for (int i = 0; i < 10000; ++i) concurrent.mouseEvent(0, i);
    });
    for (int i = 0; i < 10000; ++i) (void)concurrent.activeAt(i);
    writer.join();
    require(concurrent.activeAt(10000));
    // Installation/teardown smoke check only; no input is injected or logged.
    { InputActivityMonitor monitor; (void)monitor.hasRecentInput(); }
    { InputActivityMonitor monitor; (void)monitor.hasRecentInput(); }
    std::cout << "PASS: injected flags, expiry, keyboard/mouse, 64-bit time, concurrency, hook lifecycle\n";
}
