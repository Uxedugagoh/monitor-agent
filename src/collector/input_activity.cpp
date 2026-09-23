#include "collector/input_activity.h"
#include <windows.h>
#include <future>
#include <stdexcept>
#include <thread>

void InputActivityState::keyboardEvent(std::uint32_t flags, std::int64_t timeMs) noexcept
{
    if (!(flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)))
        lastInputMs_.store(timeMs);
}

void InputActivityState::mouseEvent(std::uint32_t flags, std::int64_t timeMs) noexcept
{
    if (!(flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED)))
        lastInputMs_.store(timeMs);
}

bool InputActivityState::activeAt(std::int64_t timeMs) const noexcept
{
    const auto last = lastInputMs_.load();
    return last >= 0 && timeMs >= last && timeMs - last < 5000;
}

struct InputActivityMonitor::Impl
{
    InputActivityState state;
    std::thread thread;
    DWORD threadId = 0;
    std::atomic<bool> failed{false};
    // Low-level callbacks run on the installing thread, so no shared pointer
    // registry or mutex is necessary inside the callbacks.
    inline static thread_local Impl* current = nullptr;

    static LRESULT CALLBACK keyboard(int code, WPARAM w, LPARAM l)
    {
        if (code == HC_ACTION && current)
        {
            const auto* event = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l);
            current->state.keyboardEvent(event->flags, static_cast<std::int64_t>(GetTickCount64()));
        }
        return CallNextHookEx(nullptr, code, w, l);
    }

    static LRESULT CALLBACK mouse(int code, WPARAM w, LPARAM l)
    {
        if (code == HC_ACTION && current)
        {
            const auto* event = reinterpret_cast<const MSLLHOOKSTRUCT*>(l);
            current->state.mouseEvent(event->flags, static_cast<std::int64_t>(GetTickCount64()));
        }
        return CallNextHookEx(nullptr, code, w, l);
    }

    void start()
    {
        std::promise<bool> ready;
        auto initialized = ready.get_future();
        thread = std::thread([this, ready = std::move(ready)]() mutable {
            current = this;
            threadId = GetCurrentThreadId();
            MSG message{};
            // Ensure the message queue exists before reporting readiness.
            PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
            const auto module = GetModuleHandleW(nullptr);
            HHOOK keys = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard, module, 0);
            HHOOK pointer = SetWindowsHookExW(WH_MOUSE_LL, mouse, module, 0);
            const bool installed = keys && pointer;
            ready.set_value(installed);
            if (installed)
            {
                int result;
                while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                if (result == -1) failed.store(true);
            }
            if (keys) UnhookWindowsHookEx(keys);
            if (pointer) UnhookWindowsHookEx(pointer);
            current = nullptr;
        });
        if (!initialized.get()) throw std::runtime_error("Cannot install keyboard/mouse activity hooks");
    }

    ~Impl()
    {
        if (threadId) PostThreadMessageW(threadId, WM_QUIT, 0, 0);
        if (thread.joinable()) thread.join();
    }
};

InputActivityMonitor::InputActivityMonitor() : impl_(std::make_unique<Impl>())
{
    impl_->start();
}

InputActivityMonitor::~InputActivityMonitor() = default;

bool InputActivityMonitor::hasRecentInput() const
{
    if (impl_->failed.load()) throw std::runtime_error("Input activity message loop failed");
    return impl_->state.activeAt(static_cast<std::int64_t>(GetTickCount64()));
}
