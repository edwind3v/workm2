#pragma once
#include <windows.h>

// Called from the world thread. Reduce speculative retention on small machines
// and when physical/process address-space headroom is low; visibility is unchanged.
inline bool IsWorldMemoryPressure()
{
    static DWORD lastCheck = 0;
    static bool pressure = true;
    const DWORD now = GetTickCount();
    if (now - lastCheck >= 1000)
    {
        MEMORYSTATUSEX memory = {};
        memory.dwLength = sizeof(memory);
        if (GlobalMemoryStatusEx(&memory))
            pressure = memory.ullTotalPhys <= 4ull * 1024ull * 1024ull * 1024ull ||
                memory.ullAvailPhys < 512ull * 1024ull * 1024ull ||
                memory.ullAvailVirtual < 512ull * 1024ull * 1024ull || memory.dwMemoryLoad >= 85;
        lastCheck = now;
    }
    return pressure;
}

// Main-thread soft budget. One indivisible engine/driver call can overrun it;
// callers must split expensive operations, and never defer required collision.
class CFrameWorkBudget
{
public:
    explicit CFrameWorkBudget(double milliseconds = 0) { Reset(milliseconds); }
    void Reset(double milliseconds)
    {
        LARGE_INTEGER frequency, now;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&now);
        m_deadline = now.QuadPart + static_cast<LONGLONG>(frequency.QuadPart * milliseconds / 1000.0);
    }
    bool Available() const
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return now.QuadPart < m_deadline;
    }
private:
    LONGLONG m_deadline = 0;
};
