#include "collector/activity_collector.h"

#include <windows.h>
#include <psapi.h>

#include <ctime>
#include <stdexcept>

namespace
{
    std::string toUtf8(const std::wstring& text)
    {
        if (text.empty())
        {
            return {};
        }

        const int length = static_cast<int>(text.size());
        const int byteCount = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
            nullptr, 0, nullptr, nullptr);
        if (byteCount == 0)
        {
            throw std::runtime_error("Failed to calculate UTF-8 size");
        }

        std::string result(byteCount, '\0');
        if (WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
                result.data(), byteCount, nullptr, nullptr) == 0)
        {
            throw std::runtime_error("Failed to convert text to UTF-8");
        }
        return result;
    }

    std::string readWindowTitle(HWND window)
    {
        const int length = GetWindowTextLengthW(window);
        if (length == 0)
        {
            return {};
        }

        // Reserve one extra character for the terminating null.
        std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
        const int copied = GetWindowTextW(window, title.data(),
            static_cast<int>(title.size()));
        title.resize(copied);
        return toUtf8(title);
    }

    std::string currentLocalTime()
    {
        const std::time_t now = std::time(nullptr);
        std::tm localTime{};
        if (now == static_cast<std::time_t>(-1) || localtime_s(&localTime, &now) != 0)
        {
            throw std::runtime_error("Failed to obtain local time");
        }

        char formatted[20]{};
        if (std::strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S", &localTime) == 0)
        {
            throw std::runtime_error("Failed to format local time");
        }
        return formatted;
    }

}

ActivityRecord ActivityCollector::collect()
{
    ActivityRecord record;

    record.time = currentLocalTime();
    record.userActive = input_.hasRecentInput();

    HWND foregroundWindow = GetForegroundWindow();

    if (foregroundWindow == nullptr)
    {
        return record;
    }

    record.windowTitle = readWindowTitle(foregroundWindow);

    DWORD processId = 0;

    GetWindowThreadProcessId(
        foregroundWindow,
        &processId
    );

    HANDLE processHandle = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
        FALSE,
        processId
    );

    if (processHandle != nullptr)
    {
        wchar_t processName[MAX_PATH]{};

        const DWORD copied = GetModuleBaseNameW(
            processHandle,
            nullptr,
            processName,
            MAX_PATH);

        // Release the handle before conversion, which can throw an exception.
        CloseHandle(processHandle);

        if (copied > 0)
        {
            record.processName = toUtf8(std::wstring(processName, copied));
        }
    }

    return record;
}
