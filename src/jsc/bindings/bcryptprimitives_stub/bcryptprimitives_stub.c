// bcryptprimitives_stub.c - XP-compatible stub for bcryptprimitives.dll
// Provides ProcessPrng, BCryptGenRandom, and ntdll Rtl* sync APIs
// via advapi32/RtlGenRandom and no-op stubs.
// Compile with: cl /LD /MT /O1 /GS- bcryptprimitives_stub.c advapi32.lib kernel32.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

// ── PRNG (bcryptprimitives) ──

typedef BOOLEAN (WINAPI *RtlGenRandom_t)(PVOID, ULONG);
static RtlGenRandom_t pRtlGenRandom;

static void ensure_rtlgenrandom(void) {
    if (!pRtlGenRandom) {
        HMODULE h = GetModuleHandleA("advapi32");
        if (h) pRtlGenRandom = (RtlGenRandom_t)GetProcAddress(h, "SystemFunction036");
    }
}

__declspec(dllexport) BOOL WINAPI ProcessPrng(PBYTE pbData, SIZE_T cbData) {
    ensure_rtlgenrandom();
    return pRtlGenRandom ? pRtlGenRandom(pbData, (ULONG)cbData) : FALSE;
}

__declspec(dllexport) LONG WINAPI BCryptGenRandom(HANDLE, PUCHAR pbBuffer, ULONG cbBuffer, ULONG) {
    ensure_rtlgenrandom();
    return pRtlGenRandom ? (pRtlGenRandom(pbBuffer, cbBuffer) ? 0 : -1) : -1;
}

// ── ntdll Rtl sync APIs (Windows 8+; not on XP) ──
// Stubs: these are used by Bun's Futex module. No-op stubs are safe:
// WaitOnAddress with zero timeout simulates "no waiters", WakeByAddress*
// is a no-op (waking zero waiters is harmless).

__declspec(dllexport) NTSTATUS WINAPI RtlWaitOnAddress(void volatile*, void*, SIZE_T, LARGE_INTEGER*) {
    return STATUS_TIMEOUT;  // No waiters ever, always time out
}

__declspec(dllexport) void WINAPI RtlWakeAddressAll(void*) {}

__declspec(dllexport) void WINAPI RtlWakeAddressSingle(void*) {}

// RtlExitUserProcess IS in XP's ntdll, but just in case:
__declspec(dllexport) void WINAPI RtlExitUserProcess(UINT uExitCode) {
    ExitProcess(uExitCode);
}
