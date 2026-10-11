#include "ps_pad.h"
#include "thread_census.h"

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

namespace pspad {

namespace {

std::int16_t Axis(std::uint8_t v, bool flip) {
    float f = (static_cast<float>(v) - 127.5f) * 257.0f;
    if (flip) f = -f;
    return static_cast<std::int16_t>(std::clamp(std::lround(f), -32768L, 32767L));
}

// Sony's report layouts (public HID descriptors; SDL and the Linux hid-sony /
// hid-playstation drivers read the same bytes). `b` starts at the left stick X.
// DS4 (and every Bluetooth "simple" 0x01 report): sticks, buttons x3, L2, R2.
// DualSense full report: sticks, L2, R2, counter, buttons x3.
void Fill(const std::uint8_t* b, bool ds5Layout, SvPad& pad) {
    const std::uint8_t b0 = ds5Layout ? b[7] : b[4];
    const std::uint8_t b1 = ds5Layout ? b[8] : b[5];
    const std::uint8_t b2 = ds5Layout ? b[9] : b[6];
    pad.left_trigger = ds5Layout ? b[4] : b[7];
    pad.right_trigger = ds5Layout ? b[5] : b[8];
    pad.left_x = Axis(b[0], false);
    pad.left_y = Axis(b[1], true);
    pad.right_x = Axis(b[2], false);
    pad.right_y = Axis(b[3], true);
    std::uint16_t x = 0;
    const int hat = b0 & 0x0F; // 0 = up, clockwise to 7 = up-left; 8 = none
    if (hat == 7 || hat == 0 || hat == 1) x |= 0x0001;
    if (hat >= 3 && hat <= 5) x |= 0x0002;
    if (hat >= 5 && hat <= 7) x |= 0x0004;
    if (hat >= 1 && hat <= 3) x |= 0x0008;
    if (b0 & 0x10) x |= 0x4000; // Square -> X
    if (b0 & 0x20) x |= 0x1000; // Cross -> A
    if (b0 & 0x40) x |= 0x2000; // Circle -> B
    if (b0 & 0x80) x |= 0x8000; // Triangle -> Y
    if (b1 & 0x01) x |= 0x0100; // L1 -> LB
    if (b1 & 0x02) x |= 0x0200; // R1 -> RB
    if (b1 & 0x10) x |= 0x0020; // Share / Create -> Back
    if (b1 & 0x20) x |= 0x0010; // Options -> Start
    if (b1 & 0x40) x |= 0x0040; // L3
    if (b1 & 0x80) x |= 0x0080; // R3
    if (b2 & 0x02) x |= 0x0020; // touchpad click -> Back too
    pad.buttons = x;
    pad.connected = 1;
}

struct Device {
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool dualSense = false;
    bool usb = false;
    USHORT length = 0;
};

bool Known(USHORT pid, bool& dualSense) {
    switch (pid) {
    case 0x05C4: case 0x09CC: case 0x0BA0: dualSense = false; return true; // DS4 v1, v2, wireless adapter
    case 0x0CE6: case 0x0DF2: dualSense = true; return true;               // DualSense, DualSense Edge
    default: return false;
    }
}

Device Find() {
    GUID guid{};
    HidD_GetHidGuid(&guid);
    Device found;
    HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return found;
    SP_DEVICE_INTERFACE_DATA iface{};
    iface.cbSize = sizeof(iface);
    for (DWORD i = 0; found.handle == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(set, nullptr, &guid, i, &iface); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &size, nullptr);
        if (!size) continue;
        std::vector<BYTE> buffer(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, size, nullptr, nullptr)) continue;
        // Attributes need no access rights (keyboards and mice refuse reads).
        HANDLE probe = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (probe == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES a{};
        a.Size = sizeof(a);
        bool dualSense = false;
        const bool sony = HidD_GetAttributes(probe, &a) && a.VendorID == 0x054C && Known(a.ProductID, dualSense);
        CloseHandle(probe);
        if (!sony) continue;
        // Shared: GTA and Steam read the same pad; each handle gets every report.
        HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        PHIDP_PREPARSED_DATA pp = nullptr;
        HIDP_CAPS caps{};
        if (HidD_GetPreparsedData(h, &pp)) {
            HidP_GetCaps(pp, &caps);
            HidD_FreePreparsedData(pp);
        }
        if (caps.UsagePage != 0x01 || caps.Usage != 0x05 || caps.InputReportByteLength < 10) {
            CloseHandle(h);
            continue;
        }
        found = {h, dualSense, caps.InputReportByteLength == 64, caps.InputReportByteLength};
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

std::mutex g_lock;
SvPad g_pad{};
const char* g_name = nullptr;

void Run() {
    threadcensus::Name(L"skatev-ps-pad");
    std::vector<std::uint8_t> report;
    for (;;) {
        const Device d = Find();
        if (d.handle == INVALID_HANDLE_VALUE) {
            Sleep(2000); // plugged in or paired later
            continue;
        }
        const char* name = d.dualSense ? (d.usb ? "DualSense (USB)" : "DualSense (Bluetooth)")
                                       : (d.usb ? "DualShock 4 (USB)" : "DualShock 4 (Bluetooth)");
        report.assign(d.length, 0);
        std::uint32_t packet = 0;
        DWORD got = 0;
        // The pads report continuously; a failed read means it went away.
        while (ReadFile(d.handle, report.data(), d.length, &got, nullptr)) {
            SvPad pad{};
            if (!Parse(report.data(), got, d.dualSense, d.usb, pad)) continue;
            pad.packet = ++packet;
            std::lock_guard lock(g_lock);
            g_pad = pad;
            g_name = name;
        }
        CloseHandle(d.handle);
        std::lock_guard lock(g_lock);
        g_pad = {};
        g_name = nullptr;
    }
}

} // namespace

bool Parse(const std::uint8_t* r, std::size_t n, bool dualSense, bool usb, SvPad& out) {
    if (!r || n < 1) return false;
    std::size_t base = 0;
    bool ds5Layout = false;
    switch (r[0]) {
    case 0x01: base = 1, ds5Layout = dualSense && usb; break; // USB, or Bluetooth's simple report
    case 0x11: if (dualSense) return false; base = 3; break;   // DS4 Bluetooth full report
    case 0x31: if (!dualSense) return false; base = 2, ds5Layout = true; break; // DualSense Bluetooth
    default: return false;
    }
    if (n < base + (ds5Layout ? 10 : 9)) return false;
    out = {};
    Fill(r + base, ds5Layout, out);
    return true;
}

bool Read(SvPad& out, const char** name) {
    static std::once_flag started;
    std::call_once(started, [] { std::thread(Run).detach(); });
    std::lock_guard lock(g_lock);
    if (name) *name = g_name;
    if (!g_name) return false;
    out = g_pad;
    return true;
}

} // namespace pspad
