#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace hid {
constexpr USAGE Consumer = 0x0c, MenuPick = 0x41;
struct Edge {
    bool down = false;
    bool update(bool now) { const bool fire = now && !down; down = now; return fire; }
};
struct Device {
    HANDLE handle{};
    std::wstring path, name;
    std::vector<BYTE> preparsed;
    std::array<bool, 256> okReports{};
    std::array<Edge, 256> edges{};
};
inline bool ValidReports(size_t available, DWORD size, DWORD count) {
    return size > 0 && count > 0 && count <= available / size;
}
inline std::wstring DevicePath(HANDLE device) {
    UINT chars = 0;
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &chars) == UINT(-1) || !chars) return {};
    std::vector<wchar_t> name(static_cast<size_t>(chars) + 1);
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name.data(), &chars) == UINT(-1)) return {};
    return name.data();
}
inline void Prepare(Device& d) {
    UINT bytes = 0;
    if (GetRawInputDeviceInfoW(d.handle, RIDI_PREPARSEDDATA, nullptr, &bytes) == UINT(-1) || !bytes) return;
    d.preparsed.resize(bytes);
    if (GetRawInputDeviceInfoW(d.handle, RIDI_PREPARSEDDATA, d.preparsed.data(), &bytes) == UINT(-1)) {
        d.preparsed.clear(); return;
    }
    auto p = reinterpret_cast<PHIDP_PREPARSED_DATA>(d.preparsed.data());
    HIDP_CAPS caps{};
    if (HidP_GetCaps(p, &caps) != HIDP_STATUS_SUCCESS || !caps.NumberInputButtonCaps) return;
    USHORT count = caps.NumberInputButtonCaps;
    std::vector<HIDP_BUTTON_CAPS> buttons(count);
    if (HidP_GetButtonCaps(HidP_Input, buttons.data(), &count, p) != HIDP_STATUS_SUCCESS) return;
    for (USHORT i = 0; i < count; ++i) {
        const auto& b = buttons[i];
        if (b.UsagePage == Consumer && (b.IsRange ?
            b.Range.UsageMin <= MenuPick && b.Range.UsageMax >= MenuPick : b.NotRange.Usage == MenuPick))
            d.okReports[b.ReportID] = true;
    }
}
// 1 = pressed, 0 = released, -1 = unrelated or unsupported. Keep report IDs separate.
inline int Decode(Device& d, const BYTE* data, DWORD length) {
    if (!data || !length) return -1;
    if (!d.preparsed.empty() && d.okReports[data[0]]) {
        auto p = reinterpret_cast<PHIDP_PREPARSED_DATA>(d.preparsed.data());
        ULONG max = HidP_MaxUsageListLength(HidP_Input, Consumer, p);
        if (max && max <= 65536) {
            std::vector<USAGE> usages(max);
            ULONG count = max;
            auto result = HidP_GetUsages(HidP_Input, Consumer, 0, usages.data(), &count, p,
                reinterpret_cast<PCHAR>(const_cast<BYTE*>(data)), length);
            if (result == HIDP_STATUS_SUCCESS && count <= max)
                return std::find(usages.begin(), usages.begin() + count, MenuPick) != usages.begin() + count ? 1 : 0;
        }
    }
    // Exact reports documented by the public G60S workaround; never substring-match.
    if (length == 3 && data[0] == 1 && data[2] == 0) {
        if (data[1] == 0x41) return 1;
        if (data[1] == 0) return 0;
    }
    return -1;
}
inline bool Enumerate(std::vector<Device>& output, DWORD& error) {
    output.clear(); error = 0;
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) {
        error = GetLastError(); return false;
    }
    std::vector<RAWINPUTDEVICELIST> list(count);
    const UINT actual = count ? GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST)) : 0;
    if (actual == UINT(-1)) { error = GetLastError(); return false; }
    for (UINT i = 0; i < actual; ++i) {
        RID_DEVICE_INFO info{}; info.cbSize = sizeof(info); UINT size = sizeof(info);
        if (list[i].dwType != RIM_TYPEHID ||
            GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICEINFO, &info, &size) == UINT(-1) ||
            info.hid.usUsagePage != Consumer || info.hid.usUsage != 1) continue;
        Device d; d.handle = list[i].hDevice; d.path = DevicePath(d.handle);
        if (d.path.empty()) continue;
        wchar_t product[256]{};
        HANDLE file = CreateFileW(d.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file != INVALID_HANDLE_VALUE) { HidD_GetProductString(file, product, sizeof(product)); CloseHandle(file); }
        wchar_t ids[80]{};
        swprintf_s(ids, L"  [VID %04lX : PID %04lX]  #%zu", info.hid.dwVendorId, info.hid.dwProductId, output.size() + 1);
        d.name = std::wstring(product[0] ? product : L"HID Consumer Control") + ids;
        Prepare(d); output.push_back(std::move(d));
    }
    return true;
}
inline std::wstring Hex(const BYTE* data, size_t n) {
    std::wstring result;
    for (size_t i = 0; i < std::min(n, size_t(32)); ++i) {
        wchar_t part[4]{}; swprintf_s(part, L"%02X ", data[i]); result += part;
    }
    if (n > 32) result += L"...";
    return result;
}
} // namespace hid
