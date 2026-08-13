// Standalone TU (includes minimal Windows headers). Provides Windows XP fallback
// implementations for APIs introduced in Vista/Win7/Win8/Win10. Two mechanisms:
//
//   Kernel32 + ws2_32 (inet_pton): the import library ALWAYS wins over
//             /alternatename, so an IAT entry is still created and XP dies at
//             load. Instead, xp_win9x_imports.asm defines the `__imp__Foo@N`
//             DATA symbol in an object ??? an object definition beats the import
//             library, so every caller resolves to our pointer below and no
//             IAT entry is created. The stub functions here are the targets.
//             Each stub must be named exactly `_Foo@N` (stdcall decoration)
//             so the .asm can point at it.
//
//   Other DLLs (IPHLPAPI, ADVAPI32, SHELL32, WS2_32 WSAPoll): delay-loaded;
//             the hook at the bottom of this file handles dliFailGetProc for
//             any function not present on XP and returns our stub.
//
// This file is compiled standalone (no PCH).  We include windows.h for types
// and the dllimport declarations; we provide definitions for the stubs.

#define WIN32_LEAN_AND_MEAN 1
#define NOMINMAX 1
#define NOGDI 1
#define NOSERVICE 1
#define NOKERNEL 1
#define NOMCX 1
#include <windows.h>
#include <delayimp.h>
#include <psapi.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include <intrin.h>

// Some types are excluded by WIN32_LEAN_AND_MEAN; provide them here.
#ifndef AF_INET
#define AF_INET 2
#define AF_INET6 23
#endif
#ifndef INADDR_NONE
#define INADDR_NONE 0xFFFFFFFF
#endif

// ntdll helpers
extern "C" LONG __stdcall NtQueryInformationFile(HANDLE, PVOID, PVOID, ULONG, ULONG);
extern "C" LONG __stdcall NtSetInformationFile(HANDLE, PVOID, PVOID, ULONG, ULONG);
extern "C" LONG __stdcall NtQueryObject(HANDLE, int, PVOID, ULONG, PULONG);



// ===================================================================
// KERNEL32 -- /alternatename redirects + implementations
//
// windows.h already declares these functions (as __declspec(dllimport)).
// We do NOT repeat the declaration -- we only provide the definition,
// matching the header's signature exactly.
// ===================================================================

// -- SRW Locks (Vista+) -- emulate with CriticalSection --
//
// SRWLOCK is only 4 bytes on x86 ??? too small to hold a CRITICAL_SECTION
// (24 bytes), so storing one inline overflows whatever follows. Instead we
// emulate each SRW lock with one of a fixed pool of critical sections keyed
// by the lock's address. This serializes exclusive+shared holders fully
// (semantically correct, just not concurrent) and is safe for statically
// zero-initialized SRWLOCKs (e.g. MSVC std::mutex/_Mtx_init).

struct XpSrwPool {
    CRITICAL_SECTION cs[256];
    volatile LONG ready;
    XpSrwPool() : ready(0) {}
    void ensure() {
        if (ready == 2) return;
        if (_InterlockedCompareExchange(&ready, 1, 0) == 0) {
            for (int i = 0; i < 256; ++i)
                InitializeCriticalSection(&cs[i]);
            _InterlockedExchange(&ready, 2);
        } else {
            while (ready == 1) {}
        }
    }
};

static XpSrwPool xp_srwPool;

static CRITICAL_SECTION* xp_srwCS(PSRWLOCK lock) {
    xp_srwPool.ensure();
    uintptr_t p = (uintptr_t)lock;
    size_t idx = ((p >> 2) + (p >> 16)) % 256;
    return &xp_srwPool.cs[idx];
}

void __stdcall AcquireSRWLockExclusive(PSRWLOCK SRWLock) {
    EnterCriticalSection(xp_srwCS(SRWLock));
}

void __stdcall AcquireSRWLockShared(PSRWLOCK SRWLock) {
    EnterCriticalSection(xp_srwCS(SRWLock));
}

void __stdcall ReleaseSRWLockExclusive(PSRWLOCK SRWLock) {
    LeaveCriticalSection(xp_srwCS(SRWLock));
}

void __stdcall ReleaseSRWLockShared(PSRWLOCK SRWLock) {
    LeaveCriticalSection(xp_srwCS(SRWLock));
}

BOOLEAN __stdcall TryAcquireSRWLockExclusive(PSRWLOCK SRWLock) {
    return TryEnterCriticalSection(xp_srwCS(SRWLock));
}

BOOLEAN __stdcall TryAcquireSRWLockShared(PSRWLOCK SRWLock) {
    return TryEnterCriticalSection(xp_srwCS(SRWLock));
}

// -- Condition Variables (Vista+) -- emulate with Event --
//
// Windows CONDITION_VARIABLEs are designed to be zero-initialized and to
// self-initialize on first use (WTF's ThreadCondition never calls
// InitializeConditionVariable). Our emulation stores a HANDLE in the 4-byte
// slot, so a zeroed CONDITION_VARIABLE has a NULL handle -> WAIT_FAILED with
// ERROR_INVALID_HANDLE. Lazy-create the event on first use instead.

struct xp_condvar { volatile LONG evt; };

static HANDLE xp_cv_ensure(xp_condvar* cv) {
    if (cv->evt)
        return (HANDLE)(intptr_t)cv->evt;
    HANDLE ne = CreateEventW(0, TRUE, FALSE, 0);
    LONG old = _InterlockedCompareExchange(&cv->evt, (LONG)(intptr_t)ne, 0);
    if (old) {
        CloseHandle(ne);
        return (HANDLE)(intptr_t)old;
    }
    return ne;
}

void __stdcall InitializeConditionVariable(PCONDITION_VARIABLE CondVar) {
    xp_cv_ensure((xp_condvar*)CondVar);
}

void __stdcall WakeConditionVariable(PCONDITION_VARIABLE CondVar) {
    auto* cv = (xp_condvar*)CondVar;
    if (cv->evt)
        SetEvent((HANDLE)(intptr_t)cv->evt);
}

void __stdcall WakeAllConditionVariable(PCONDITION_VARIABLE CondVar) {
    auto* cv = (xp_condvar*)CondVar;
    if (cv->evt)
        SetEvent((HANDLE)(intptr_t)cv->evt);
}

BOOL __stdcall SleepConditionVariableCS(PCONDITION_VARIABLE CondVar, PCRITICAL_SECTION CriticalSection, DWORD dwMilliseconds) {
    auto* cv = (xp_condvar*)CondVar;
    HANDLE evt = xp_cv_ensure(cv);
    LeaveCriticalSection(CriticalSection);
    DWORD res = WaitForSingleObject(evt, dwMilliseconds);
    EnterCriticalSection(CriticalSection);
    return res == WAIT_OBJECT_0;
}

BOOL __stdcall SleepConditionVariableSRW(PCONDITION_VARIABLE CondVar, PSRWLOCK SRWLock, DWORD dwMilliseconds, ULONG) {
    return SleepConditionVariableCS(CondVar, xp_srwCS(SRWLock), dwMilliseconds);
}

// -- One-Time Initialization (Vista+) --

struct xp_init_once {
    volatile LONG state; // 0 = not started, 1 = in progress, 2 = done
};

#define XP_INITONCE_SLOTS 256
static struct { void* addr; void* ctx; } xp_io_ctx[XP_INITONCE_SLOTS];
static volatile LONG xp_io_lock = 0;

static void xp_io_lock_acquire() { while (_InterlockedExchange(&xp_io_lock, 1)) SleepEx(0, TRUE); }
static void xp_io_lock_release() { _InterlockedExchange(&xp_io_lock, 0); }

static void xp_io_store_ctx(void* addr, void* ctx) {
    xp_io_lock_acquire();
    for (int i = 0; i < XP_INITONCE_SLOTS; i++) {
        if (!xp_io_ctx[i].addr) { xp_io_ctx[i].addr = addr; xp_io_ctx[i].ctx = ctx; break; }
    }
    xp_io_lock_release();
}
static void* xp_io_load_ctx(void* addr) {
    void* ctx = 0;
    xp_io_lock_acquire();
    for (int i = 0; i < XP_INITONCE_SLOTS; i++) {
        if (xp_io_ctx[i].addr == addr) { ctx = xp_io_ctx[i].ctx; break; }
    }
    xp_io_lock_release();
    return ctx;
}

BOOL __stdcall InitOnceBeginInitialize(PINIT_ONCE InitOnce, DWORD dwFlags, PBOOL fPending, LPVOID* lpContext) {
    auto* io = (xp_init_once*)InitOnce;
    if (dwFlags & INIT_ONCE_ASYNC) {
        if (_InterlockedCompareExchange(&io->state, 1, 0) == 0) {
            if (fPending) *fPending = TRUE;
            if (lpContext) *lpContext = 0;
            return TRUE;
        }
        if (fPending) *fPending = FALSE;
        if (lpContext) *lpContext = (io->state == 2) ? xp_io_load_ctx(InitOnce) : 0;
        return TRUE;
    }
    for (;;) {
        LONG s = io->state;
        if (s == 0) {
            if (_InterlockedCompareExchange(&io->state, 1, 0) == 0) {
                if (fPending) *fPending = TRUE;
                if (lpContext) *lpContext = 0;
                return TRUE;
            }
            continue;
        }
        if (s == 1) { SleepEx(1, FALSE); continue; }
        if (fPending) *fPending = FALSE;
        if (lpContext) *lpContext = xp_io_load_ctx(InitOnce);
        return TRUE;
    }
}

BOOL __stdcall InitOnceComplete(PINIT_ONCE InitOnce, DWORD dwFlags, LPVOID lpContext) {
    auto* io = (xp_init_once*)InitOnce;
    if (dwFlags & INIT_ONCE_INIT_FAILED) {
        _InterlockedExchange(&io->state, 0);
        return TRUE;
    }
    if (lpContext) xp_io_store_ctx(InitOnce, lpContext);
    _InterlockedExchange(&io->state, 2);
    return TRUE;
}

BOOL __stdcall InitOnceExecuteOnce(PINIT_ONCE InitOnce, PINIT_ONCE_FN InitFn, LPVOID Parameter, LPVOID* Context) {
    auto* io = (xp_init_once*)InitOnce;
    if (_InterlockedCompareExchange(&io->state, 1, 0) == 0) {
        BOOL result = InitFn(InitOnce, Parameter, Context);
        if (Context && *Context) xp_io_store_ctx(InitOnce, *Context);
        _InterlockedExchange(&io->state, 2);
        return result;
    }
    while (io->state != 2) SleepEx(1, FALSE);
    if (Context) *Context = xp_io_load_ctx(InitOnce);
    return TRUE;
}

// -- Critical Section Extensions (Vista+) --

BOOL __stdcall InitializeCriticalSectionEx(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount, DWORD Flags) {
    InitializeCriticalSectionAndSpinCount(lpCriticalSection, dwSpinCount);
    return TRUE;
}

void __stdcall InitializeSRWLock(PSRWLOCK SRWLock) {}

// -- Interlocked SList (Vista+ InterlockedFlushSList; XP only has
//    InterlockedPushEntrySList / InterlockedPopEntrySList) --

PSLIST_ENTRY __stdcall InterlockedFlushSList(PSLIST_HEADER ListHead) {
    PSLIST_ENTRY first = 0;
    PSLIST_ENTRY entry;
    while ((entry = InterlockedPopEntrySList(ListHead)) != 0) {
        if (!first) first = entry;
    }
    return first;
}

// -- Event/Semaphore/Timer Extended Create (Vista+) --

HANDLE __stdcall CreateEventExW(LPSECURITY_ATTRIBUTES lpEventAttributes, LPCWSTR lpName, DWORD dwFlags, DWORD dwDesiredAccess) {
    return CreateEventW(lpEventAttributes, (dwFlags & CREATE_EVENT_MANUAL_RESET) ? TRUE : FALSE,
                        (dwFlags & CREATE_EVENT_INITIAL_SET) ? TRUE : FALSE, lpName);
}

HANDLE __stdcall CreateSemaphoreExW(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, LPCWSTR lpName, DWORD dwFlags, DWORD dwDesiredAccess) {
    return CreateSemaphoreW(lpSemaphoreAttributes, lInitialCount, lMaximumCount, lpName);
}

HANDLE __stdcall CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES lpTimerAttributes, LPCWSTR lpTimerName, DWORD dwFlags, DWORD dwDesiredAccess) {
    typedef HANDLE (WINAPI* CreateWaitableTimerWFunc)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
    static CreateWaitableTimerWFunc real = 0;
    if (!real) {
        HMODULE mod = GetModuleHandleA("kernel32.dll");
        if (mod) real = (CreateWaitableTimerWFunc)GetProcAddress(mod, "CreateWaitableTimerW");
        if (!real) return 0;
    }
    return real(lpTimerAttributes, FALSE, lpTimerName);
}

// -- Thread Pool (Vista+) -- no-op stubs --
// extern "C": these aren't dllimport-declared in the headers (WIN32_LEAN_AND_MEAN
// + _WIN32_WINNT guards hide the declarations), so without it clang-cl C++-mangles
// the names and the __imp_ data symbols in xp_win9x_imports.asm can't find them.

void __stdcall CloseThreadpoolTimer(PTP_TIMER) {}

void __stdcall CloseThreadpoolWait(PTP_WAIT) {}

PTP_TIMER __stdcall CreateThreadpoolTimer(PTP_TIMER_CALLBACK, PVOID, PTP_CALLBACK_ENVIRON) { return 0; }

PTP_WAIT __stdcall CreateThreadpoolWait(PTP_WAIT_CALLBACK, PVOID, PTP_CALLBACK_ENVIRON) { return 0; }

extern "C" void __stdcall bun_SetThreadpoolTimer(PTP_TIMER, PFILETIME, DWORD, DWORD, DWORD) {}

extern "C" void __stdcall bun_SetThreadpoolTimer4(PTP_TIMER, PFILETIME, DWORD, DWORD) {}

void __stdcall SetThreadpoolWait(PTP_WAIT, HANDLE, PFILETIME) {}

void __stdcall WaitForThreadpoolTimerCallbacks(PTP_TIMER, BOOL) {}

void __stdcall FreeLibraryWhenCallbackReturns(PTP_CALLBACK_INSTANCE, HMODULE) {}

// -- File APIs (Vista+) --

HANDLE __stdcall CreateFile2(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwCreationDisposition, LPCREATEFILE2_EXTENDED_PARAMETERS pCreateExParams) {
    return CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, 0, dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, 0);
}

BOOL __stdcall GetFileInformationByHandleEx(HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS FileInformationClass, LPVOID lpFileInformation, DWORD dwBufferSize) {
    if (FileInformationClass == FileBasicInfo) {
        struct { LARGE_INTEGER times[4]; DWORD FileAttributes; } info = {};
        if (dwBufferSize < sizeof(info)) return FALSE;
        return FALSE;
    }
    return FALSE;
}

BOOL __stdcall SetFileInformationByHandle(HANDLE, FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD) {
    return FALSE;
}

DWORD __stdcall GetFinalPathNameByHandleW(HANDLE hFile, LPWSTR lpszFilePath, DWORD cchFilePath, DWORD dwFlags) {
    // Prefer the real API when present (Vista+); fall back to the XP emulation.
    static DWORD (WINAPI* real)(HANDLE, LPWSTR, DWORD, DWORD) = 0;
    if (!real) {
        HMODULE mod = GetModuleHandleA("kernel32.dll");
        if (mod) real = (DWORD (WINAPI*)(HANDLE, LPWSTR, DWORD, DWORD))GetProcAddress(mod, "GetFinalPathNameByHandleW");
    }
    if (real) return real(hFile, lpszFilePath, cchFilePath, dwFlags);

    // XP emulation: NtQueryObject(ObjectNameInformation) yields the NT device
    // name (e.g. "\Device\HarddiskVolume2\WS\foo"); QueryDosDeviceW maps the
    // volume device prefix back to a drive letter so we can spell the DOS form.
    typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } XP_UNICODE_STRING;
    typedef struct { XP_UNICODE_STRING Name; } XP_OBJECT_NAME_INFORMATION;

    if (!lpszFilePath || !cchFilePath) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    ULONG cap = 1024;
    XP_OBJECT_NAME_INFORMATION* info = (XP_OBJECT_NAME_INFORMATION*)malloc(cap);
    if (!info) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    ULONG needed = 0;
    LONG st = NtQueryObject(hFile, 1, info, cap, &needed);
    if (st == 0xC0000004 /* STATUS_INFO_LENGTH_MISMATCH */ && needed > cap) {
        free(info);
        info = (XP_OBJECT_NAME_INFORMATION*)malloc(needed);
        if (!info) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        cap = needed;
        st = NtQueryObject(hFile, 1, info, cap, &needed);
    }
    if (st < 0 || !info->Name.Buffer || info->Name.Length == 0) {
        free(info);
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }

    size_t ntlen = info->Name.Length / sizeof(WCHAR);
    const WCHAR* nt = info->Name.Buffer;
    int normalized = (dwFlags & 0x1) ? 1 : 0;
    DWORD vol = dwFlags & 0x7; // 0/1 DOS, 2/3 NT, 4/5 NONE

    if (vol == 0x2 || vol == 0x3) {
        // VOLUME_NAME_NT -- return the raw NT name.
        if (ntlen >= cchFilePath) {
            free(info);
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return 0;
        }
        memcpy(lpszFilePath, nt, ntlen * sizeof(WCHAR));
        lpszFilePath[ntlen] = 0;
        free(info);
        return (DWORD)ntlen;
    }

    // Map the longest matching volume device prefix to a drive letter.
    size_t best = 0;
    WCHAR bestLetter = 0;
    if (ntlen >= 2 && nt[0] == L'\\' && nt[1] == L'\\') {
        // NT device names are "\Device\...".
        for (WCHAR d = L'A'; d <= L'Z'; ++d) {
            WCHAR drv[4] = { d, L':', 0, 0 };
            WCHAR dev[512];
            DWORD dn = QueryDosDeviceW(drv, dev, 512);
            size_t dl = 0;
            if (dn) {
                dl = wcslen(dev);
            } else if (GetLastError() == ERROR_MORE_DATA) {
                dl = 512;
            }
            if (dl > 0 && dl < ntlen && _wcsnicmp(nt, dev, dl) == 0 && nt[dl] == L'\\') {
                if (dl >= best) {
                    best = dl;
                    bestLetter = d;
                }
            }
        }
    }

    // Strip the matched device prefix; rest is "\WS\foo" (or server\share for UNC).
    const WCHAR* rest = nt + best;
    size_t restlen = ntlen - best;

    if (vol == 0x4 || vol == 0x5) {
        // VOLUME_NAME_NONE -- no volume portion; keep the leading '\'.
        if (restlen + 1 >= cchFilePath) {
            free(info);
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return 0;
        }
        memcpy(lpszFilePath, rest, restlen * sizeof(WCHAR));
        lpszFilePath[restlen] = 0;
        free(info);
        return (DWORD)restlen;
    }

    if (bestLetter && best) {
        // DOS form: "D:\WS\foo" (or "\\?\D:\WS\foo" when normalized).
        size_t neededLen = 3 + restlen; // drive + ':' + rest
        if (normalized)
            neededLen += 4; // \\?\ prefix
        if (neededLen >= cchFilePath) {
            free(info);
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return 0;
        }
        WCHAR* out = lpszFilePath;
        if (normalized) {
            out[0] = L'\\'; out[1] = L'\\'; out[2] = L'?'; out[3] = L'\\';
            out += 4;
        }
        out[0] = bestLetter;
        out[1] = L':';
        memcpy(out + 2, rest, restlen * sizeof(WCHAR));
        out[2 + restlen] = 0;
        free(info);
        return (DWORD)neededLen;
    }

    // UNC or unmapped volume: spell "\\server\share\..." from the NT name if
    // possible, otherwise fail rather than return a wrong path.
    free(info);
    SetLastError(ERROR_FILE_NOT_FOUND);
    return 0;
}

HANDLE __stdcall ReOpenFile(HANDLE, DWORD, DWORD, DWORD) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return INVALID_HANDLE_VALUE;
}

// -- Thread / Stack APIs (Vista+) --

VOID __stdcall GetCurrentThreadStackLimits(ULONG_PTR* LowLimit, ULONG_PTR* HighLimit) {
    if (LowLimit)
        *LowLimit = __readfsdword(0x08);
    if (HighLimit)
        *HighLimit = __readfsdword(0x04);
}

HRESULT __stdcall SetThreadDescription(HANDLE, PCWSTR) {
    return HRESULT_FROM_WIN32(ERROR_CALL_NOT_IMPLEMENTED);
}

HRESULT __stdcall GetThreadDescription(HANDLE, PWSTR*) {
    return HRESULT_FROM_WIN32(ERROR_CALL_NOT_IMPLEMENTED);
}

// -- Processor / System Info (Vista+) --

DWORD __stdcall GetCurrentProcessorNumber() {
    typedef DWORD (WINAPI* NtGCPN)();
    static NtGCPN real = 0;
    if (!real) {
        HMODULE mod = GetModuleHandleA("ntdll.dll");
        if (mod) real = (NtGCPN)GetProcAddress(mod, "NtGetCurrentProcessorNumber");
        if (!real) return 0;
    }
    return real();
}

DWORD __stdcall GetActiveProcessorCount(WORD GroupNumber) {
    if (GroupNumber != ALL_PROCESSOR_GROUPS) return 0;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
}

void __stdcall FlushProcessWriteBuffers() {
    volatile LONG tmp = 0;
    _InterlockedExchange(&tmp, 0);
}

// -- Time (Vista+/Win8) --

ULONGLONG __stdcall GetTickCount64() {
    static ULONGLONG base = 0;
    static DWORD last = 0;
    DWORD now = GetTickCount();
    if (now < last) base += 0x100000000ULL;
    last = now;
    return base + now;
}

void __stdcall GetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime) {
    GetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
}

extern "C" BOOL __stdcall bun_GetTimeZoneInformationForYear(USHORT, LPTIME_ZONE_INFORMATION, LPTIME_ZONE_INFORMATION lpTzi) {
    return GetTimeZoneInformation(lpTzi);
}

// GetDynamicTimeZoneInformation (Vista+): ICU's common/wintz.cpp uses it to
// fetch the current time zone. XP only has GetTimeZoneInformation (static TZ);
// map the call to it and leave the dynamic fields zeroed.
DWORD __stdcall GetDynamicTimeZoneInformation(DYNAMIC_TIME_ZONE_INFORMATION* pdtzi) {
    if (pdtzi == 0) return TIME_ZONE_ID_INVALID;
    TIME_ZONE_INFORMATION tzi;
    DWORD ret = GetTimeZoneInformation(&tzi);
    memset(pdtzi, 0, sizeof(*pdtzi));
    memcpy(pdtzi, &tzi, sizeof(TIME_ZONE_INFORMATION));
    return ret;
}

// -- Cancel I/O (Vista+) --

BOOL __stdcall CancelIoEx(HANDLE hFile, LPOVERLAPPED lpOverlapped) {
    if (lpOverlapped == 0) return CancelIo(hFile);
    SetLastError(ERROR_NOT_FOUND);
    return FALSE;
}

BOOL __stdcall CancelSynchronousIo(HANDLE) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

// -- Locale / String APIs (Vista+) --

extern "C" int __stdcall bun_CompareStringEx(LPCWSTR lpLocaleName, DWORD dwCmpFlags, LPCWSTR lpString1, int cchCount1, LPCWSTR lpString2, int cchCount2, PVOID lpVersionInformation, PVOID lpReserved, LPARAM lParam) {
    LCID lcid = (lpLocaleName == 0) ? LOCALE_USER_DEFAULT : GetUserDefaultLCID();
    return CompareStringW(lcid, dwCmpFlags, lpString1, cchCount1, lpString2, cchCount2);
}

int __stdcall CompareStringOrdinal(LPCWSTR lpString1, int cchCount1, LPCWSTR lpString2, int cchCount2, BOOL bIgnoreCase) {
    if (bIgnoreCase) return CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, lpString1, cchCount1, lpString2, cchCount2) - 2;
    int minLen = (cchCount1 < cchCount2) ? cchCount1 : cchCount2;
    if (cchCount1 < 0) { cchCount1 = 0; while (lpString1[cchCount1]) cchCount1++; }
    for (int i = 0; i < minLen; i++) {
        if (lpString1[i] < lpString2[i]) return -1;
        if (lpString1[i] > lpString2[i]) return 1;
    }
    if (cchCount1 < cchCount2) return -1;
    if (cchCount1 > cchCount2) return 1;
    return 0;
}

int __stdcall GetLocaleInfoEx(LPCWSTR lpLocaleName, LCTYPE LCType, LPWSTR lpLCData, int cchData) {
    return GetLocaleInfoW(GetUserDefaultLCID(), LCType, lpLCData, cchData);
}

extern "C" int __stdcall bun_LCMapStringEx(LPCWSTR lpLocaleName, DWORD dwMapFlags, LPCWSTR lpSrcStr, int cchSrc, LPWSTR lpDestStr, int cchDest, PVOID lpVersionInformation, PVOID lpReserved, LPARAM lParam) {
    LCID lcid = (lpLocaleName == 0) ? LOCALE_USER_DEFAULT : GetUserDefaultLCID();
    return LCMapStringW(lcid, dwMapFlags, lpSrcStr, cchSrc, lpDestStr, cchDest);
}

LANGID __stdcall GetUserDefaultUILanguage() {
    return (LANGID)(GetUserDefaultLCID() & 0xFFFF);
}

// -- Currency/Date/Number/Time format Ex variants (Vista+) --
// ICU's winnmfmt.cpp calls the *Ex locale functions that XP lacks. Delegate to
// the XP-era non-Ex variants with the user-default LCID, matching the
// CompareStringEx/LCMapStringEx simplification above.

extern "C" int __stdcall GetCurrencyFormatEx(LPCWSTR lpLocaleName, DWORD dwFlags, LPCWSTR lpValue, const CURRENCYFMTW* lpFormat, LPWSTR lpCurrencyStr, int cchCurrency) {
    LCID lcid = (lpLocaleName == 0) ? LOCALE_USER_DEFAULT : GetUserDefaultLCID();
    return GetCurrencyFormatW(lcid, dwFlags, lpValue, lpFormat, lpCurrencyStr, cchCurrency);
}

extern "C" int __stdcall GetNumberFormatEx(LPCWSTR lpLocaleName, DWORD dwFlags, LPCWSTR lpValue, const NUMBERFMTW* lpFormat, LPWSTR lpNumberStr, int cchNumber) {
    LCID lcid = (lpLocaleName == 0) ? LOCALE_USER_DEFAULT : GetUserDefaultLCID();
    return GetNumberFormatW(lcid, dwFlags, lpValue, lpFormat, lpNumberStr, cchNumber);
}

extern "C" int __stdcall GetDateFormatEx(LPCWSTR lpLocaleName, DWORD dwFlags, const SYSTEMTIME* lpDate, LPCWSTR lpFormat, LPWSTR lpDateStr, int cchDate, LPCWSTR lpCalendar) {
    LCID lcid = (lpLocaleName == 0) ? LOCALE_USER_DEFAULT : GetUserDefaultLCID();
    (void)lpCalendar;
    return GetDateFormatW(lcid, dwFlags, lpDate, lpFormat, lpDateStr, cchDate);
}

extern "C" int __stdcall GetTimeFormatEx(LPCWSTR lpLocaleName, DWORD dwFlags, const SYSTEMTIME* lpTime, LPCWSTR lpFormat, LPWSTR lpTimeStr, int cchTime) {
    LCID lcid = (lpLocaleName == 0) ? LOCALE_USER_DEFAULT : GetUserDefaultLCID();
    return GetTimeFormatW(lcid, dwFlags, lpTime, lpFormat, lpTimeStr, cchTime);
}

// -- Locale name <-> LCID (Vista+) --
// ICU's common/locmap.cpp converts between Windows locale names and LCIDs.
// XP has neither LocaleNameToLCID nor LCIDToLocaleName, so synthesize both.

// LocaleNameToLCID: ICU accepts any LCID > 0 and explicitly blesses
// LOCALE_USER_DEFAULT round-tripping (locmap.cpp), so we return it.
extern "C" LCID __stdcall LocaleNameToLCID(LPCWSTR lpName, DWORD dwFlags) {
    (void)dwFlags;
    return (lpName == 0) ? 0 : LOCALE_USER_DEFAULT;
}

// LCIDToLocaleName: build "xx-YY" from the LCID's ISO 639 language + ISO 3166
// region codes (GetLocaleInfoW), matching ICU's use in locmap.cpp.
extern "C" int __stdcall LCIDToLocaleName(LCID Locale, LPWSTR lpName, int cchName, DWORD dwFlags) {
    (void)dwFlags;
    wchar_t lang[16] = {0}, region[16] = {0};
    int langLen = GetLocaleInfoW(Locale, LOCALE_SISO639LANGNAME, lang, 16);
    int regionLen = GetLocaleInfoW(Locale, LOCALE_SISO3166CTRYNAME, region, 16);
    if (langLen <= 1) return 0;
    if (regionLen <= 1) { region[0] = L'Z'; region[1] = L'Z'; region[2] = 0; regionLen = 3; }
    int needed = langLen + regionLen; // "xx\0" + "YY\0" -> "xx-YY\0"
    if (cchName == 0) return needed;
    if (lpName == 0 || cchName < needed) return 0;
    int pos = 0;
    for (int i = 0; i < langLen - 1; i++) lpName[pos++] = lang[i];
    lpName[pos++] = L'-';
    for (int i = 0; i < regionLen - 1; i++) lpName[pos++] = region[i];
    lpName[pos++] = 0;
    return needed;
}

// ResolveLocaleName: normalize a BCP-47 tag to a Windows locale name. ICU
// (winnmfmt.cpp) accepts whatever comes back; identity is a safe fallback.
extern "C" int __stdcall ResolveLocaleName(LPCWSTR lpNameToResolve, LPWSTR lpLocaleName, int cchLocaleName) {
    if (lpNameToResolve == 0) return 0;
    int len = 0;
    while (lpNameToResolve[len]) len++;
    int needed = len + 1;
    if (cchLocaleName == 0) return needed;
    if (cchLocaleName < needed) return 0;
    for (int i = 0; i <= len; i++) lpLocaleName[i] = lpNameToResolve[i];
    return needed;
}

// -- Named Pipe (Vista+) --

BOOL __stdcall GetNamedPipeClientProcessId(HANDLE, PULONG) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE;
}

BOOL __stdcall GetNamedPipeServerProcessId(HANDLE, PULONG) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE;
}

// -- Completion Port (Vista+) --

BOOL __stdcall GetQueuedCompletionStatusEx(HANDLE CompletionPort, LPOVERLAPPED_ENTRY lpCompletionPortEntries, ULONG ulCount, PULONG ulNumEntriesRemoved, DWORD dwMilliseconds, BOOL fAlertable) {
    ULONG num = 0;
    for (ULONG i = 0; i < ulCount; i++) {
        LPOVERLAPPED overlap = 0;
        DWORD xfer = 0;
        ULONG_PTR key = 0;
        if (GetQueuedCompletionStatus(CompletionPort, &xfer, &key, &overlap, (i == 0) ? dwMilliseconds : 0)) {
            if (lpCompletionPortEntries) {
                lpCompletionPortEntries[num].lpOverlapped = overlap;
                lpCompletionPortEntries[num].dwNumberOfBytesTransferred = xfer;
                lpCompletionPortEntries[num].lpCompletionKey = key;
            }
            num++;
        } else {
            DWORD err = GetLastError();
            /* A failed OVERLAPPED operation is still a dequeued completion: the
             * caller (libuv's IOCP loop) inspects the OVERLAPPED to learn the
             * per-op error (e.g. ERROR_BROKEN_PIPE -> UV_EOF). GetQueuedCompletionStatus
             * returns FALSE for those with GetLastError() set to the op's error;
             * only a genuine WAIT_TIMEOUT means nothing was dequeued. Deliver the
             * failed completion so libuv processes it instead of treating the
             * error as a fatal IOCP failure. */
            if (err != WAIT_TIMEOUT && overlap) {
                if (lpCompletionPortEntries) {
                    lpCompletionPortEntries[num].lpOverlapped = overlap;
                    lpCompletionPortEntries[num].dwNumberOfBytesTransferred = xfer;
                    lpCompletionPortEntries[num].lpCompletionKey = key;
                }
                num++;
            } else {
                break;
            }
        }
    }
    *ulNumEntriesRemoved = num;
    return (num > 0) ? TRUE : FALSE;
}

// -- Process/Thread Attribute List (Vista+) --

BOOL __stdcall InitializeProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList, DWORD dwAttributeCount, DWORD dwFlags, PSIZE_T lpSize) {
    if (lpSize == 0) return FALSE;
    SIZE_T needed = sizeof(PVOID) + dwAttributeCount * sizeof(PVOID);
    if (*lpSize < needed) { *lpSize = needed; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    *lpSize = needed;
    if (lpAttributeList) *(PVOID*)lpAttributeList = 0;
    return TRUE;
}

BOOL __stdcall UpdateProcThreadAttribute(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD_PTR, PVOID, SIZE_T, PVOID, PSIZE_T) {
    return TRUE;
}

void __stdcall DeleteProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST) {}

// -- Symbolic Link (Vista+) --

BOOLEAN __stdcall CreateSymbolicLinkW(LPCWSTR, LPCWSTR, DWORD) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE;
}

// -- PseudoConsole / Terminal (Win10) --

void __stdcall ClosePseudoConsole(HPCON) {}

extern "C" HRESULT __stdcall bun_CreatePseudoConsole(HANDLE, HANDLE, DWORD, DWORD, HPCON*) {
    return E_NOTIMPL;
}

HRESULT __stdcall ResizePseudoConsole(HPCON, COORD) {
    return E_NOTIMPL;
}

// -- Misc --

BOOL __stdcall NeedCurrentDirectoryForExePathW(LPCWSTR) {
    return TRUE;
}

BOOL __stdcall SetFileCompletionNotificationModes(HANDLE, UCHAR) {
    return TRUE;
}

BOOL __stdcall K32EnumProcessModules(HANDLE hProcess, HMODULE* lphModule, DWORD cb, LPDWORD lpcbNeeded) {
    static BOOL (WINAPI* realEnum)(HANDLE, HMODULE*, DWORD, LPDWORD) = 0;
    if (!realEnum) {
        HMODULE mod = GetModuleHandleA("psapi.dll");
        if (!mod) mod = LoadLibraryW(L"psapi.dll");
        if (mod) realEnum = (BOOL (WINAPI*)(HANDLE, HMODULE*, DWORD, LPDWORD))GetProcAddress(mod, "EnumProcessModules");
        if (!realEnum) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE; }
    }
    return realEnum(hProcess, lphModule, cb, lpcbNeeded);
}

// -- K32* (Win7) -- forward to psapi.dll, which XP has --

static FARPROC xp_psapiProc(const char* name) {
    static HMODULE mod = 0;
    if (!mod) {
        mod = GetModuleHandleA("psapi.dll");
        if (!mod) mod = LoadLibraryW(L"psapi.dll");
        if (!mod) return 0;
    }
    return GetProcAddress(mod, name);
}

DWORD __stdcall K32GetModuleBaseNameW(HANDLE hProcess, HMODULE hModule, LPWSTR lpBaseName, DWORD nSize) {
    typedef DWORD (WINAPI* Fn)(HANDLE, HMODULE, LPWSTR, DWORD);
    static Fn real = 0;
    if (!real) real = (Fn)xp_psapiProc("GetModuleBaseNameW");
    if (!real) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return 0; }
    return real(hProcess, hModule, lpBaseName, nSize);
}

DWORD __stdcall K32GetModuleFileNameExW(HANDLE hProcess, HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    typedef DWORD (WINAPI* Fn)(HANDLE, HMODULE, LPWSTR, DWORD);
    static Fn real = 0;
    if (!real) real = (Fn)xp_psapiProc("GetModuleFileNameExW");
    if (!real) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return 0; }
    return real(hProcess, hModule, lpFilename, nSize);
}

BOOL __stdcall K32GetProcessMemoryInfo(HANDLE hProcess, PPROCESS_MEMORY_COUNTERS ppsmemCounters, DWORD cb) {
    typedef BOOL (WINAPI* Fn)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    static Fn real = 0;
    if (!real) real = (Fn)xp_psapiProc("GetProcessMemoryInfo");
    if (!real) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE; }
    return real(hProcess, ppsmemCounters, cb);
}

BOOL __stdcall K32QueryWorkingSet(HANDLE hProcess, PVOID pv, DWORD cb) {
    typedef BOOL (WINAPI* Fn)(HANDLE, PVOID, DWORD);
    static Fn real = 0;
    if (!real) real = (Fn)xp_psapiProc("QueryWorkingSet");
    if (!real) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE; }
    return real(hProcess, pv, cb);
}

// -- Job Objects (Win2000+ SetInformationJobObject; absent on Win9x/ME) --

typedef BOOL (WINAPI* SetInformationJobObjectFn)(HANDLE, JOBOBJECTINFOCLASS, LPVOID, DWORD);

BOOL __stdcall SetInformationJobObject(HANDLE hJob, JOBOBJECTINFOCLASS C, LPVOID p, DWORD cb) {
    // Job Objects are Windows 2000+/WinXP; only Win9x lacks them. Prefer the
    // real export when present so kill-on-close (test runner, watcher) works
    // on Win9x2K/XP rather than always failing.
    static SetInformationJobObjectFn real = 0;
    if (!real) {
        HMODULE mod = GetModuleHandleA("kernel32.dll");
        if (mod) real = (SetInformationJobObjectFn)GetProcAddress(mod, "SetInformationJobObject");
        if (!real) {
            SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
            return FALSE;
        }
    }
    return real(hJob, C, p, cb);
}

// -- Memory Resource Notifications (Vista+) --

extern "C" HANDLE __stdcall bun_CreateMemoryResourceNotification(DWORD) {
    // Return a valid, closable handle so callers can WaitForSingleObject on it.
    return CreateEventW(0, TRUE, FALSE, 0);
}

BOOL __stdcall QueryMemoryResourceNotification(HANDLE, PBOOL pbGood) {
    if (pbGood) *pbGood = TRUE;
    return TRUE;
}

// ===================================================================
// WS2_32 -- inet_ntop / inet_pton (Vista+)
// These must be extern "C" because their callers include headers that
// declare them without dllimport.
// ===================================================================

// inet_ntop / inet_pton -- no dllimport from ws2tcpip.h in this TU.
// We define them here with the correct stdcall decorated names for x86.
// xp_win9x_imports.asm defines __imp__inet_pton@12 pointing at _inet_pton@12,
// so the ws2_32.dll import (Vista+) never enters the IAT.
extern "C" {
PCSTR __stdcall inet_ntop(INT Family, const VOID* pAddr, PSTR pStringBuf, SIZE_T StringBufSize) {
    if (Family == AF_INET) {
        const auto* addr = (const unsigned char*)pAddr;
        _snprintf(pStringBuf, StringBufSize, "%u.%u.%u.%u", addr[0], addr[1], addr[2], addr[3]);
        return pStringBuf;
    }
    if (Family == AF_INET6) return 0;
    SetLastError(WSAEAFNOSUPPORT);
    return 0;
}

INT __stdcall inet_pton(INT Family, PCSTR pStringBuf, PVOID pAddr) {
    if (Family == AF_INET) {
        unsigned char* addr = (unsigned char*)pAddr;
        unsigned b1, b2, b3, b4;
        if (sscanf(pStringBuf, "%u.%u.%u.%u", &b1, &b2, &b3, &b4) == 4 &&
            b1 < 256 && b2 < 256 && b3 < 256 && b4 < 256) {
            addr[0] = (unsigned char)b1; addr[1] = (unsigned char)b2;
            addr[2] = (unsigned char)b3; addr[3] = (unsigned char)b4;
            return 1;
        }
        return 0;
    }
    if (Family == AF_INET6) return 0;
    SetLastError(WSAEAFNOSUPPORT);
    return -1;
}
}

// ===================================================================
// WS2_32 -- GetAddrInfoW / FreeAddrInfoW (Vista+)
// libuv's src/win/getaddrinfo.c imports the *W DNS entry points, but XP only
// exports the ANSI getaddrinfo/freeaddrinfo. xp_win9x_imports.asm redirects
// __imp__GetAddrInfoW@16 / __imp__FreeAddrInfoW@4 to these polyfills so no
// ws2_32 IAT entry is ever created; the load-time 0xC0000139 goes away.
// The addrinfo layouts used here match win/ws2tcpip.h exactly.
// ===================================================================

struct xp_addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    char* ai_canonname;
    struct sockaddr* ai_addr;
    struct xp_addrinfo* ai_next;
};

struct xp_addrinfow {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    wchar_t* ai_canonname;
    struct sockaddr* ai_addr;
    struct xp_addrinfow* ai_next;
    // Owned allocations, freed by FreeAddrInfoW below.
    void* _name;
    void* _addr;
};

// sockaddr is excluded by WIN32_LEAN_AND_MEAN; it is used only as an opaque
// pointer type here, so the implicit forward declaration is sufficient.

// WSAAPI / error codes may not be present under WIN32_LEAN_AND_MEAN.
#ifndef WSAAPI
#define WSAAPI __stdcall
#endif
#ifndef WSAEINVAL
#define WSAEINVAL 10022
#endif
#ifndef WSA_NOT_ENOUGH_MEMORY
#define WSA_NOT_ENOUGH_MEMORY (10055 - 101) /* EAI_MEMORY, as used by winsock */
#endif
#ifndef WSA_E_CANCELLED
#define WSA_E_CANCELLED 0x2741
#endif

// ANSI versions live in ws2_32.dll on XP too (the build already imports them
// via uSockets' bsd.c), so just declare + reuse the existing imports. These
// match ws2tcpip.h's getaddrinfo/freeaddrinfo shapes exactly.
extern "C" {
int WSAAPI getaddrinfo(const char*, const char*, const struct xp_addrinfo*, struct xp_addrinfo**);
void WSAAPI freeaddrinfo(struct xp_addrinfo*);
void __stdcall FreeAddrInfoW(struct xp_addrinfow*);
}

extern "C" {

int __stdcall GetAddrInfoW(const wchar_t* pwszNodeName,
                           const wchar_t* pwszServiceName,
                           const struct xp_addrinfow* hints,
                           struct xp_addrinfow** ppResult) {
    if (!ppResult) return WSAEINVAL;
    *ppResult = 0;

    char nodeBuf[513] = {0};
    char servBuf[65] = {0};
    const char* nodeA = 0;
    const char* servA = 0;
    if (pwszNodeName) {
        if (0 == WideCharToMultiByte(CP_UTF8, 0, pwszNodeName, -1, nodeBuf, sizeof nodeBuf, 0, 0))
            return WSA_E_CANCELLED; /* host names are ASCII; undecodable => unsupported */
        nodeA = nodeBuf;
    }
    if (pwszServiceName) {
        if (0 == WideCharToMultiByte(CP_UTF8, 0, pwszServiceName, -1, servBuf, sizeof servBuf, 0, 0))
            return WSA_E_CANCELLED;
        servA = servBuf;
    }

    struct xp_addrinfo ahints = {};
    struct xp_addrinfo* ahintsp = 0;
    if (hints) {
        ahints.ai_flags = hints->ai_flags;
        ahints.ai_family = hints->ai_family;
        ahints.ai_socktype = hints->ai_socktype;
        ahints.ai_protocol = hints->ai_protocol;
        ahintsp = &ahints;
    }

    struct xp_addrinfo* ares = 0;
    int err = getaddrinfo(nodeA, servA, ahintsp, &ares);
    if (err) return err;

    // Convert the ANSI chain into a W chain we own; FreeAddrInfoW walks it.
    struct xp_addrinfow* tail = *ppResult;
    struct xp_addrinfow* prev = 0;
    struct xp_addrinfo* a = ares;
    while (a) {
        struct xp_addrinfow* next = 0;
        struct xp_addrinfow* n = (struct xp_addrinfow*)malloc(sizeof *n);
        if (!n) { freeaddrinfo(ares); FreeAddrInfoW(*ppResult); return WSA_NOT_ENOUGH_MEMORY; }
        memset(n, 0, sizeof *n);
        n->ai_flags = a->ai_flags;
        n->ai_family = a->ai_family;
        n->ai_socktype = a->ai_socktype;
        n->ai_protocol = a->ai_protocol;
        n->ai_addrlen = a->ai_addrlen;
        if (a->ai_addr && a->ai_addrlen) {
            n->_addr = malloc(a->ai_addrlen);
            if (!n->_addr) { free(n); freeaddrinfo(ares); FreeAddrInfoW(*ppResult); return WSA_NOT_ENOUGH_MEMORY; }
            memcpy(n->_addr, a->ai_addr, a->ai_addrlen);
            n->ai_addr = (struct sockaddr*)n->_addr;
        }
        if (a->ai_canonname) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, a->ai_canonname, -1, 0, 0);
            if (wlen > 0) {
                n->_name = malloc((size_t)wlen * sizeof(wchar_t));
                if (n->_name) {
                    MultiByteToWideChar(CP_UTF8, 0, a->ai_canonname, -1, (wchar_t*)n->_name, wlen);
                    n->ai_canonname = (wchar_t*)n->_name;
                }
            }
        }
        if (prev) prev->ai_next = n;
        else *ppResult = n;
        prev = n;
        tail = n;
        a = a->ai_next;
    }
    if (tail) tail->ai_next = 0;

    freeaddrinfo(ares);
    return 0;
}

void __stdcall FreeAddrInfoW(struct xp_addrinfow* pAddrInfo) {
    struct xp_addrinfow* cur = pAddrInfo;
    while (cur) {
        struct xp_addrinfow* next = cur->ai_next;
        if (cur->_name) free(cur->_name);
        if (cur->_addr) free(cur->_addr);
        free(cur);
        cur = next;
    }
}

}

// ===================================================================
// IPHLPAPI -- 10 missing functions (Delay-loaded + hook below)
// ===================================================================

typedef USHORT ADDRESS_FAMILY;

extern "C" {

DWORD __stdcall xp_CancelMibChangeNotify2(PVOID) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return (DWORD)-1;
}

DWORD __stdcall xp_ConvertInterfaceIndexToLuid(ULONG, PVOID) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return ERROR_INVALID_PARAMETER;
}

DWORD __stdcall xp_ConvertInterfaceLuidToNameA(PVOID, PSTR, SIZE_T) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return ERROR_INVALID_PARAMETER;
}

DWORD __stdcall xp_ConvertInterfaceLuidToNameW(PVOID, PWSTR, SIZE_T) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return ERROR_INVALID_PARAMETER;
}

void __stdcall xp_FreeMibTable(PVOID) {}

DWORD __stdcall xp_GetBestRoute2(PVOID, PVOID, ULONG, PVOID, ULONG, PVOID, PVOID) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return ERROR_INSUFFICIENT_BUFFER;
}

DWORD __stdcall xp_GetUnicastIpAddressTable(ADDRESS_FAMILY, PVOID*) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return ERROR_NOT_SUPPORTED;
}

DWORD __stdcall xp_NotifyIpInterfaceChange(PVOID, PVOID, BOOLEAN, PVOID) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return (DWORD)-1;
}

DWORD __stdcall xp_if_indextoname(ULONG, PSTR) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return 0;
}

DWORD __stdcall xp_if_nametoindex(PCSTR) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return 0;
}
}

// ===================================================================
// ADVAPI32 -- RegGetValueW (Vista+)
// ===================================================================

extern "C" {

LSTATUS __stdcall xp_RegGetValueW(HKEY hKey, LPCWSTR lpSubKey, LPCWSTR lpValue, DWORD dwFlags, LPDWORD pdwType, PVOID pvData, LPDWORD pcbData) {
    HKEY hSubKey = 0;
    LSTATUS res = RegOpenKeyExW(hKey, lpSubKey, 0, KEY_READ, &hSubKey);
    if (res != ERROR_SUCCESS) return res;
    res = RegQueryValueExW(hSubKey, lpValue, 0, pdwType, (LPBYTE)pvData, pcbData);
    RegCloseKey(hSubKey);
    return res;
}
}

// ===================================================================
// SHELL32 -- SHGetKnownFolderPath (Vista+)
// ===================================================================

extern "C" {

HRESULT __stdcall xp_SHGetKnownFolderPath(REFGUID rfid, DWORD dwFlags, HANDLE hToken, PWSTR* ppszPath) {
    return E_NOTIMPL;
}
}

// ===================================================================
// Delay-load failure hook -- handles WS2_32 + IPHLPAPI + ADVAPI32 +
// SHELL32 missing exports on XP
// ===================================================================

// WSAPoll polyfill (defined in wsapoll_stub.cpp)
struct wsapoll_pollfd;
extern "C" int __stdcall bun_wsapoll_stub(wsapoll_pollfd*, unsigned long, int);

static FARPROC WINAPI xp_delayHook(unsigned dliNotify, PDelayLoadInfo pdli) {
    if (dliNotify == dliFailGetProc) {
        // WS2_32: WSAPoll (ordinal 46) is Vista+ and imported by ordinal, so
        // it can't be redirected via __imp_ data symbols ??? the delay-load hook
        // returns the select() polyfill. inet_pton is handled by the __imp_
        // data symbol in xp_win9x_imports.asm (not via this hook).
        if (pdli->szDll && _stricmp(pdli->szDll, "WS2_32.dll") == 0) {
            if (!pdli->dlp.fImportByName && pdli->dlp.dwOrdinal == 46)
                return (FARPROC)bun_wsapoll_stub;
            if (pdli->dlp.fImportByName && pdli->dlp.szProcName &&
                strcmp(pdli->dlp.szProcName, "WSAPoll") == 0)
                return (FARPROC)bun_wsapoll_stub;
        }

        if (pdli->szDll && _stricmp(pdli->szDll, "IPHLPAPI.DLL") == 0) {
            const char* procName = pdli->dlp.fImportByName ? pdli->dlp.szProcName : 0;
            if (!procName) return 0;
            if (strcmp(procName, "CancelMibChangeNotify2") == 0)    return (FARPROC)xp_CancelMibChangeNotify2;
            if (strcmp(procName, "ConvertInterfaceIndexToLuid") == 0) return (FARPROC)xp_ConvertInterfaceIndexToLuid;
            if (strcmp(procName, "ConvertInterfaceLuidToNameA") == 0) return (FARPROC)xp_ConvertInterfaceLuidToNameA;
            if (strcmp(procName, "ConvertInterfaceLuidToNameW") == 0) return (FARPROC)xp_ConvertInterfaceLuidToNameW;
            if (strcmp(procName, "FreeMibTable") == 0)              return (FARPROC)xp_FreeMibTable;
            if (strcmp(procName, "GetBestRoute2") == 0)             return (FARPROC)xp_GetBestRoute2;
            if (strcmp(procName, "GetUnicastIpAddressTable") == 0)  return (FARPROC)xp_GetUnicastIpAddressTable;
            if (strcmp(procName, "NotifyIpInterfaceChange") == 0)   return (FARPROC)xp_NotifyIpInterfaceChange;
            if (strcmp(procName, "if_indextoname") == 0)            return (FARPROC)xp_if_indextoname;
            if (strcmp(procName, "if_nametoindex") == 0)            return (FARPROC)xp_if_nametoindex;
        }

        if (pdli->szDll && _stricmp(pdli->szDll, "ADVAPI32.dll") == 0) {
            const char* procName = pdli->dlp.fImportByName ? pdli->dlp.szProcName : 0;
            if (procName && strcmp(procName, "RegGetValueW") == 0)
                return (FARPROC)xp_RegGetValueW;
        }

        if (pdli->szDll && _stricmp(pdli->szDll, "SHELL32.dll") == 0) {
            const char* procName = pdli->dlp.fImportByName ? pdli->dlp.szProcName : 0;
            if (procName && strcmp(procName, "SHGetKnownFolderPath") == 0)
                return (FARPROC)xp_SHGetKnownFolderPath;
        }
    }
    return 0;
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = xp_delayHook;
extern "C" const PfnDliHook __pfnDliFailureHook2 = xp_delayHook;




