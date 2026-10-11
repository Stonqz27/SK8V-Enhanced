// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <span>
#include <vector>

namespace threadpatch {
struct Write { std::uintptr_t target; std::uint64_t expected; std::uint64_t value; };
// Prepare handles and page permissions before stopping game workers. No heap
// allocation, logging or CRT work happens while another thread is suspended.
// If any instruction pointer is inside a changing prologue, retry later.
inline bool Apply(std::span<const Write> writes) {
    struct Page { void* address; DWORD old; };
    struct Thread { HANDLE handle; bool suspended; };
    std::vector<Page> pages;
    std::vector<Thread> threads;
    pages.reserve(writes.size()); threads.reserve(256);
    for (const auto& w:writes) {
        if ((w.target&7) || *reinterpret_cast<const std::uint64_t*>(w.target)!=w.expected) return false;
    }
    const DWORD process=GetCurrentProcessId(), self=GetCurrentThreadId();
    const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot==INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 entry{}; entry.dwSize=sizeof entry;
    bool ok=Thread32First(snapshot,&entry)!=FALSE;
    if(ok) do {
        if(entry.th32OwnerProcessID!=process || entry.th32ThreadID==self) continue;
        const HANDLE h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|SYNCHRONIZE,FALSE,entry.th32ThreadID);
        if(!h) { ok=false; break; }
        threads.push_back({h,false});
    } while(Thread32Next(snapshot,&entry));
    CloseHandle(snapshot);
    if(ok) for(const auto& w:writes) {
        DWORD old=0;
        if(!VirtualProtect(reinterpret_cast<void*>(w.target),8,PAGE_EXECUTE_READWRITE,&old)) {ok=false;break;}
        pages.push_back({reinterpret_cast<void*>(w.target),old});
    }
    if(ok) for(auto& t:threads) {
        if(WaitForSingleObject(t.handle,0)==WAIT_OBJECT_0) continue;
        if(SuspendThread(t.handle)==DWORD(-1)) {ok=false;break;}
        t.suspended=true;
        CONTEXT context{}; context.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(t.handle,&context)) {ok=false;break;}
        for(const auto& w:writes)
            if(context.Rip>=w.target && context.Rip<w.target+8) {ok=false;break;}
        if(!ok) break;
    }
    if(ok) for(const auto& w:writes)
        if(*reinterpret_cast<const std::uint64_t*>(w.target)!=w.expected) {ok=false;break;}
    if(ok) {
        for(const auto& w:writes) InterlockedExchange64(reinterpret_cast<volatile LONG64*>(w.target),w.value);
        FlushInstructionCache(GetCurrentProcess(),nullptr,0);
    }
    for(auto& t:threads) if(t.suspended) ResumeThread(t.handle);
    // Restore repeated pages in reverse order so they regain their original
    // executable protection even when both functions occupy the same page.
    for(auto p=pages.rbegin();p!=pages.rend();++p) {DWORD unused;VirtualProtect(p->address,8,p->old,&unused);}
    for(auto& t:threads) CloseHandle(t.handle);
    return ok;
}
}
