// Standalone TU (no includes, no PCH). Defines Win8+ API set symbols that
// don't exist on Windows XP/Win9x. Produces x86 stdcall decorated names
// matching synchronization.lib, so the linker resolves them instead of
// importing from api-ms-win-core-synch-l1-2-0.dll (excluded via
// /NODEFAULTLIB:synchronization.lib in flags.ts).
//
// Rust's -Zbuild-std uses dllimport on x86 MSVC targets, which generates
// references to __imp__WaitOnAddress@16 etc. These references are redirected
// via pragma alternatename (below) to the _WaitOnAddress@16 symbols defined
// here, preventing the linker from extracting the import object members from
// bun_rust.lib.

#pragma comment(linker, "/alternatename:__imp__WaitOnAddress@16=_WaitOnAddress@16")
#pragma comment(linker, "/alternatename:__imp__WakeByAddressAll@4=_WakeByAddressAll@4")
#pragma comment(linker, "/alternatename:__imp__WakeByAddressSingle@4=_WakeByAddressSingle@4")
#pragma comment(linker, "/alternatename:__imp__ProcessPrng@8=_ProcessPrng@8")
#pragma comment(linker, "/alternatename:__imp__RtlWaitOnAddress@16=_RtlWaitOnAddress@16")
#pragma comment(linker, "/alternatename:__imp__RtlWakeAddressAll@4=_RtlWakeAddressAll@4")
#pragma comment(linker, "/alternatename:__imp__RtlWakeAddressSingle@4=_RtlWakeAddressSingle@4")
#pragma comment(linker, "/alternatename:__imp__RtlExitUserProcess@4=_RtlExitUserProcess@4")
#pragma comment(linker, "/alternatename:__imp__RtlRestoreContext@8=_RtlRestoreContext@8")


extern "C" {

int __stdcall WaitOnAddress(void volatile*, void*, unsigned long, unsigned long) { return 0; }
void __stdcall WakeByAddressAll(void*) {}
void __stdcall WakeByAddressSingle(void*) {}

int __stdcall SystemFunction036(void*, unsigned long);

int __stdcall ProcessPrng(void* pbData, unsigned long cbData) {
    return SystemFunction036(pbData, cbData);
}

int __stdcall BCryptGenRandom(void*, void* pbBuffer, unsigned long cbBuffer, unsigned long) {
    return SystemFunction036(pbBuffer, cbBuffer) ? 0 : -1;
}

// Kernel32 helpers (declared so this standalone TU needs no includes).
unsigned long __stdcall GetTickCount(void);
int __stdcall SwitchToThread(void);

// ntdll Rtl sync APIs (Windows 8+); not available on XP. Bun's Futex module
// (src/threading/Futex.rs) uses RtlWaitOnAddress and PANICS on any return
// code other than STATUS_SUCCESS (0) or STATUS_TIMEOUT (0x102). Implement a
// real spin-wait: block while *Address == *CompareAddress, yielding so other
// threads (module loader / event loop) can run and change the value.

int __stdcall RtlWaitOnAddress(void volatile* Address, void* CompareAddress, unsigned long AddressSize, void* Timeout) {
    unsigned long deadline = 0;
    bool hasDeadline = false;
    if (Timeout != nullptr) {
        long long timeout = *(long long*)Timeout;
        if (timeout != 0) {
            // Negative LARGE_INTEGER = relative 100ns units. Coarse deadline via
            // GetTickCount (ms).
            deadline = GetTickCount() + (unsigned long)((-timeout) / 10000);
            hasDeadline = true;
        }
    }
    for (;;) {
        bool equal = true;
        const unsigned char* a = (const unsigned char*)Address;
        const unsigned char* c = (const unsigned char*)CompareAddress;
        for (unsigned long i = 0; i < AddressSize; i++) {
            if (a[i] != c[i]) { equal = false; break; }
        }
        if (!equal) return 0; // STATUS_SUCCESS — value changed
        if (hasDeadline && (long)(GetTickCount() - deadline) >= 0)
            return 0x102; // STATUS_TIMEOUT
        SwitchToThread();
    }
}

void __stdcall RtlWakeAddressAll(void*) {}

void __stdcall RtlWakeAddressSingle(void*) {}

// RtlRestoreContext: ntdll export differs between x86 cdecl and stdcall.
// Our stub provides the stdcall version (@8) directly.

void __stdcall RtlRestoreContext(void*, void*) {}

// ntdll RtlExitUserProcess: not available on XP (only RtlExitUserThread).
void __stdcall ExitProcess(unsigned int);

void __stdcall RtlExitUserProcess(unsigned long uExitCode) {
    ExitProcess(uExitCode);
}

}
