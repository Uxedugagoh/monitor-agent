#include "runtime/shutdown_monitor.h"
#include <windows.h>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

struct ShutdownMonitor::Impl
{
    std::function<void(bool)> request;
    std::thread messages;
    HWND window = nullptr;
    bool registered = false;
    inline static std::mutex dispatchMutex;
    inline static Impl* active = nullptr;

    static BOOL WINAPI consoleHandler(DWORD event)
    {
        std::lock_guard<std::mutex> lock(dispatchMutex);
        if (!active) return FALSE;
        switch (event)
        {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            active->request(false);
            return TRUE;
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            // Windows may terminate the process as soon as this returns.
            active->request(true);
            return TRUE;
        default:
            return FALSE;
        }
    }

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        if (message == WM_NCCREATE)
        {
            auto* creation = reinterpret_cast<CREATESTRUCTW*>(l);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_QUERYENDSESSION) return TRUE;
        if (message == WM_ENDSESSION)
        {
            // A cancelled shutdown (w == FALSE) must leave the agent running.
            if (w && self) self->request(true);
            return 0;
        }
        if (message == WM_CLOSE)
        {
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY)
        {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, w, l);
    }

    explicit Impl(std::function<void(bool)> callback) : request(std::move(callback)) {}

    void start()
    {
        std::promise<HWND> ready;
        auto initialized = ready.get_future();
        messages = std::thread([this, ready = std::move(ready)]() mutable {
            const auto instance = GetModuleHandleW(nullptr);
            const wchar_t* className = L"MonitorAgentShutdownWindow";
            WNDCLASSW definition{};
            definition.lpfnWndProc = windowProc;
            definition.hInstance = instance;
            definition.lpszClassName = className;
            if (!RegisterClassW(&definition))
            {
                ready.set_value(nullptr);
                return;
            }
            // A hidden top-level window receives session broadcasts; a
            // message-only HWND_MESSAGE window would not receive them.
            HWND created = CreateWindowExW(0, className, L"MonitorAgentShutdown",
                0, 0, 0, 0, 0, nullptr, nullptr, instance, this);
            ready.set_value(created);
            if (created)
            {
                MSG message{};
                int result;
                while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                if (result == -1)
                {
                    request(true);
                    DestroyWindow(created);
                }
            }
            UnregisterClassW(className, instance);
        });
        window = initialized.get();
        if (!window) throw std::runtime_error("Cannot create shutdown notification window");
        std::lock_guard<std::mutex> lock(dispatchMutex);
        if (active) throw std::runtime_error("Shutdown monitor already registered");
        active = this;
        if (!SetConsoleCtrlHandler(consoleHandler, TRUE))
        {
            active = nullptr;
            throw std::runtime_error("Cannot register console shutdown handler");
        }
        registered = true;
    }

    ~Impl()
    {
        if (registered)
        {
            SetConsoleCtrlHandler(consoleHandler, FALSE);
            // Wait for an active console callback before releasing its context.
            std::lock_guard<std::mutex> lock(dispatchMutex);
            if (active == this) active = nullptr;
        }
        if (window) PostMessageW(window, WM_CLOSE, 0, 0);
        if (messages.joinable()) messages.join();
    }
};

ShutdownMonitor::ShutdownMonitor(std::function<void(bool)> request)
    : impl_(std::make_unique<Impl>(std::move(request)))
{
    impl_->start();
}

ShutdownMonitor::~ShutdownMonitor() = default;
