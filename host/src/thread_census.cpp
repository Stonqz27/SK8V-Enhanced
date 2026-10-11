#include "thread_census.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace threadcensus {
namespace {

using QueryFn = LONG(NTAPI*)(HANDLE, int, void*, ULONG, ULONG*);
using DescribeFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
using NameFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
constexpr int kStartAddress = 9; // ThreadQuerySetWin32StartAddress

std::unordered_map<DWORD, ULONGLONG> g_last; // thread id -> CPU time (100 ns)

std::string Label(HANDLE h) {
    static const auto describe = reinterpret_cast<DescribeFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
    static const auto query = reinterpret_cast<QueryFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    char out[96] = "";
    PWSTR desc = nullptr;
    if (describe && SUCCEEDED(describe(h, &desc)) && desc) {
        if (*desc) WideCharToMultiByte(CP_UTF8, 0, desc, -1, out, sizeof(out), nullptr, nullptr);
        LocalFree(desc);
    }
    if (*out) return out;
    void* start = nullptr;
    if (!query || query(h, kStartAddress, &start, sizeof(start), nullptr) != 0 || !start) return "?";
    HMODULE m = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<const char*>(start), &m))
        GetModuleFileNameA(m, path, sizeof(path));
    const char* name = std::max(std::strrchr(path, '\\'), std::strrchr(path, '/'));
    std::snprintf(out, sizeof(out), "%s+0x%llx", name ? name + 1 : path,
                  static_cast<unsigned long long>(static_cast<const char*>(start) - reinterpret_cast<const char*>(m)));
    return out;
}

} // namespace

void Name(const wchar_t* name) {
    static const auto set = reinterpret_cast<NameFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"));
    if (set) set(GetCurrentThread(), name);
}

std::string Report(double seconds, int top) {
    struct Row {
        double ms;
        DWORD id;
        std::string label;
    };
    std::vector<Row> rows;
    std::unordered_map<DWORD, ULONGLONG> now;
    const DWORD pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return "no thread snapshot";
    THREADENTRY32 te{sizeof(te)};
    double total = 0.0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        // Full query access reads a start address; limited still gives times.
        HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        FILETIME c, e, k, u;
        if (GetThreadTimes(h, &c, &e, &k, &u)) {
            const ULONGLONG cpu = (ULONGLONG(k.dwHighDateTime) << 32 | k.dwLowDateTime) +
                                  (ULONGLONG(u.dwHighDateTime) << 32 | u.dwLowDateTime);
            now[te.th32ThreadID] = cpu;
            const auto was = g_last.find(te.th32ThreadID);
            if (was != g_last.end() && seconds > 0.0) {
                const double ms = (cpu - was->second) / 1e4 / seconds;
                total += ms;
                if (ms >= 1.0) rows.push_back({ms, te.th32ThreadID, Label(h)});
            }
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    g_last.swap(now);
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.ms > b.ms; });
    char head[64];
    std::snprintf(head, sizeof(head), "%.0f ms/s over %zu threads", total, g_last.size());
    std::string s = head;
    for (int i = 0; i < top && i < static_cast<int>(rows.size()); ++i) {
        char item[160];
        std::snprintf(item, sizeof(item), "%s %s [%lu] %.0f", i ? "," : ";", rows[i].label.c_str(), rows[i].id,
                      rows[i].ms);
        s += item;
    }
    return s;
}

} // namespace threadcensus
