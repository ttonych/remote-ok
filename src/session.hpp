#pragma once
#include <windows.h>
#include <string>

namespace session {
constexpr const wchar_t* DefaultBase = L"Local\\G60S_OK";
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
inline std::wstring PriorityName(DWORD pid, const std::wstring& base = DefaultBase) { return base + L"_KodiPriority_" + std::to_wstring(pid); }
inline bool KodiHasPriority(DWORD pid, const std::wstring& base = DefaultBase) {
    Handle marker(OpenEventW(SYNCHRONIZE, FALSE, PriorityName(pid, base).c_str()));
    return marker.value && WaitForSingleObject(marker.value, 0) == WAIT_OBJECT_0;
}
struct Instance {
    std::wstring base;
    Handle own, legacyGuard, compatible, priority;
    explicit Instance(std::wstring name = DefaultBase) : base(std::move(name)) {}
    // Return 3 for an old incompatible helper or a duplicate instance, 1 for errors.
    int Acquire(DWORD kodiPid) {
        Handle gate(CreateMutexW(nullptr, FALSE, (base + L"_v2_Start").c_str()));
        if (!gate.value) return 1;
        const DWORD wait = WaitForSingleObject(gate.value, 5000);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return 1;
        struct Unlock { HANDLE h; ~Unlock() { ReleaseMutex(h); } } unlock{gate.value};
        legacyGuard.value = CreateMutexW(nullptr, FALSE, (base + L"_Native_1").c_str());
        const bool existingLegacy = GetLastError() == ERROR_ALREADY_EXISTS;
        if (!legacyGuard.value) return 1;
        Handle modern(OpenEventW(SYNCHRONIZE, FALSE, (base + L"_v2_Compatible").c_str()));
        if (existingLegacy && !modern.value) return 3;
        const std::wstring name = kodiPid ? base + L"_v2_Kodi_" + std::to_wstring(kodiPid) : base + L"_v2_Desktop";
        own.value = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!own.value) return 1;
        if (GetLastError() == ERROR_ALREADY_EXISTS) return 3;
        // Presence blocks old 1.0 helpers; v2 instances can coexist through the marker.
        compatible.value = CreateEventW(nullptr, TRUE, TRUE, (base + L"_v2_Compatible").c_str());
        if (!legacyGuard.value || !compatible.value) return 1;
        if (kodiPid) {
            priority.value = CreateEventW(nullptr, TRUE, TRUE, PriorityName(kodiPid, base).c_str());
            if (!priority.value) return 1;
        }
        return 0;
    }
};
} // namespace session
