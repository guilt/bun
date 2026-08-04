; Win9x (Windows XP) import stubs â€” redirects Vista+/Win7+/Win8+/Win10+
; kernel32.dll and ws2_32.dll imports to local polyfills in xp_compat.cpp.
;
; The linker resolves `__imp__Foo@N` references from an IMPORT LIBRARY first,
; so /alternatename can never override a symbol the library provides â€” the IAT
; entry is created and XP dies at load. Defining `__imp__Foo@N` as a real DATA
; symbol in an object beats the import library: every caller resolves to our
; pointer, no IAT entry, no kernel32/ws2_32 import. Each data symbol points at
; the same-named polyfill `_Foo@N` defined in xp_compat.cpp.
;
; win32 COFF, i586. Data symbols plus a few code thunks.
;
; The `_Foo@N` thunks below exist because their polyfills in xp_compat.cpp
; must carry `bun_` names (the SDK headers declare those functions with C++
; linkage, so `extern "C" Foo` would collide). Callers that reference the plain
; function symbol `_Foo@N` (not __imp__) would otherwise pull kernel32.lib's
; thunk + IAT, duplicating our __imp__ data symbol and crashing lld. Note
; libcpmtd.lib (MSVC C++ runtime) declares SetThreadpoolTimer with 4 args
; (`__imp__SetThreadpoolTimer@16`), hence the separate @16 data symbol + thunk.

BITS 32

extern _AcquireSRWLockExclusive@4
extern _AcquireSRWLockShared@4
extern _CancelIoEx@8
extern _CancelSynchronousIo@4
extern _ClosePseudoConsole@4
extern _CloseThreadpoolTimer@4
extern _CloseThreadpoolWait@4
extern _bun_CompareStringEx@36
extern _CompareStringOrdinal@20
extern _CreateEventExW@16
extern _CreateFile2@20
extern _bun_CreateMemoryResourceNotification@4
extern _bun_CreatePseudoConsole@20
extern _CreateSemaphoreExW@24
extern _CreateSymbolicLinkW@12
extern _CreateThreadpoolTimer@12
extern _CreateThreadpoolWait@12
extern _CreateWaitableTimerExW@16
extern _DeleteProcThreadAttributeList@4
extern _FlushProcessWriteBuffers@0
extern _FreeLibraryWhenCallbackReturns@8
extern _GetActiveProcessorCount@4
extern _GetCurrentProcessorNumber@0
extern _GetCurrentThreadStackLimits@8
extern _GetDynamicTimeZoneInformation@4
extern _GetFileInformationByHandleEx@16
extern _GetFinalPathNameByHandleW@16
extern _GetCurrencyFormatEx@24
extern _GetDateFormatEx@28
extern _GetLocaleInfoEx@16
extern _GetNumberFormatEx@24
extern _GetTimeFormatEx@24
extern _GetNamedPipeClientProcessId@8
extern _GetNamedPipeServerProcessId@8
extern _GetQueuedCompletionStatusEx@24
extern _GetSystemTimePreciseAsFileTime@4
extern _GetThreadDescription@8
extern _GetTickCount64@0
extern _bun_GetTimeZoneInformationForYear@12
extern _GetUserDefaultUILanguage@0
extern _InitOnceBeginInitialize@16
extern _InitOnceComplete@12
extern _InitOnceExecuteOnce@16
extern _InitializeConditionVariable@4
extern _InitializeCriticalSectionEx@12
extern _InitializeProcThreadAttributeList@16
extern _InitializeSRWLock@4
extern _InterlockedFlushSList@4
extern _K32EnumProcessModules@16
extern _K32GetModuleBaseNameW@16
extern _K32GetModuleFileNameExW@16
extern _K32GetProcessMemoryInfo@12
extern _K32QueryWorkingSet@12
extern _bun_LCMapStringEx@36
extern _LCIDToLocaleName@16
extern _LocaleNameToLCID@8
extern _NeedCurrentDirectoryForExePathW@4
extern _QueryMemoryResourceNotification@8
extern _ReOpenFile@16
extern _ReleaseSRWLockExclusive@4
extern _ReleaseSRWLockShared@4
extern _ResizePseudoConsole@8
extern _ResolveLocaleName@12
extern _SetFileCompletionNotificationModes@8
extern _SetFileInformationByHandle@16
extern _SetInformationJobObject@16
extern _bun_SetThreadpoolTimer@20
extern _bun_SetThreadpoolTimer4@16
extern _SetThreadpoolWait@12
extern _SleepConditionVariableCS@12
extern _SleepConditionVariableSRW@16
extern _TryAcquireSRWLockExclusive@4
extern _TryAcquireSRWLockShared@4
extern _UpdateProcThreadAttribute@28
extern _WakeAllConditionVariable@4
extern _WakeConditionVariable@4
extern _WaitForThreadpoolTimerCallbacks@8
extern _inet_pton@12

section .data

global __imp__AcquireSRWLockExclusive@4
__imp__AcquireSRWLockExclusive@4: dd _AcquireSRWLockExclusive@4

global __imp__AcquireSRWLockShared@4
__imp__AcquireSRWLockShared@4: dd _AcquireSRWLockShared@4

global __imp__CancelIoEx@8
__imp__CancelIoEx@8: dd _CancelIoEx@8

global __imp__CancelSynchronousIo@4
__imp__CancelSynchronousIo@4: dd _CancelSynchronousIo@4

global __imp__ClosePseudoConsole@4
__imp__ClosePseudoConsole@4: dd _ClosePseudoConsole@4

global __imp__CloseThreadpoolTimer@4
__imp__CloseThreadpoolTimer@4: dd _CloseThreadpoolTimer@4

global __imp__CloseThreadpoolWait@4
__imp__CloseThreadpoolWait@4: dd _CloseThreadpoolWait@4

global __imp__CompareStringEx@36
__imp__CompareStringEx@36: dd _bun_CompareStringEx@36

global __imp__CompareStringOrdinal@20
__imp__CompareStringOrdinal@20: dd _CompareStringOrdinal@20

global __imp__CreateEventExW@16
__imp__CreateEventExW@16: dd _CreateEventExW@16

global __imp__CreateFile2@20
__imp__CreateFile2@20: dd _CreateFile2@20

global __imp__CreateMemoryResourceNotification@4
__imp__CreateMemoryResourceNotification@4: dd _bun_CreateMemoryResourceNotification@4

global __imp__CreatePseudoConsole@20
__imp__CreatePseudoConsole@20: dd _bun_CreatePseudoConsole@20

global __imp__CreateSemaphoreExW@24
__imp__CreateSemaphoreExW@24: dd _CreateSemaphoreExW@24

global __imp__CreateSymbolicLinkW@12
__imp__CreateSymbolicLinkW@12: dd _CreateSymbolicLinkW@12

global __imp__CreateThreadpoolTimer@12
__imp__CreateThreadpoolTimer@12: dd _CreateThreadpoolTimer@12

global __imp__CreateThreadpoolWait@12
__imp__CreateThreadpoolWait@12: dd _CreateThreadpoolWait@12

global __imp__CreateWaitableTimerExW@16
__imp__CreateWaitableTimerExW@16: dd _CreateWaitableTimerExW@16

global __imp__DeleteProcThreadAttributeList@4
__imp__DeleteProcThreadAttributeList@4: dd _DeleteProcThreadAttributeList@4

global __imp__FlushProcessWriteBuffers@0
__imp__FlushProcessWriteBuffers@0: dd _FlushProcessWriteBuffers@0

global __imp__FreeLibraryWhenCallbackReturns@8
__imp__FreeLibraryWhenCallbackReturns@8: dd _FreeLibraryWhenCallbackReturns@8

global __imp__GetActiveProcessorCount@4
__imp__GetActiveProcessorCount@4: dd _GetActiveProcessorCount@4

global __imp__GetCurrentProcessorNumber@0
__imp__GetCurrentProcessorNumber@0: dd _GetCurrentProcessorNumber@0

global __imp__GetCurrentThreadStackLimits@8
__imp__GetCurrentThreadStackLimits@8: dd _GetCurrentThreadStackLimits@8

global __imp__GetDynamicTimeZoneInformation@4
__imp__GetDynamicTimeZoneInformation@4: dd _GetDynamicTimeZoneInformation@4

global __imp__GetFileInformationByHandleEx@16
__imp__GetFileInformationByHandleEx@16: dd _GetFileInformationByHandleEx@16

global __imp__GetFinalPathNameByHandleW@16
__imp__GetFinalPathNameByHandleW@16: dd _GetFinalPathNameByHandleW@16

global __imp__GetCurrencyFormatEx@24
__imp__GetCurrencyFormatEx@24: dd _GetCurrencyFormatEx@24

global __imp__GetDateFormatEx@28
__imp__GetDateFormatEx@28: dd _GetDateFormatEx@28

global __imp__GetNumberFormatEx@24
__imp__GetNumberFormatEx@24: dd _GetNumberFormatEx@24

global __imp__GetTimeFormatEx@24
__imp__GetTimeFormatEx@24: dd _GetTimeFormatEx@24

global __imp__GetLocaleInfoEx@16
__imp__GetLocaleInfoEx@16: dd _GetLocaleInfoEx@16

global __imp__GetNamedPipeClientProcessId@8
__imp__GetNamedPipeClientProcessId@8: dd _GetNamedPipeClientProcessId@8

global __imp__GetNamedPipeServerProcessId@8
__imp__GetNamedPipeServerProcessId@8: dd _GetNamedPipeServerProcessId@8

global __imp__GetQueuedCompletionStatusEx@24
__imp__GetQueuedCompletionStatusEx@24: dd _GetQueuedCompletionStatusEx@24

global __imp__GetSystemTimePreciseAsFileTime@4
__imp__GetSystemTimePreciseAsFileTime@4: dd _GetSystemTimePreciseAsFileTime@4

global __imp__GetThreadDescription@8
__imp__GetThreadDescription@8: dd _GetThreadDescription@8

global __imp__GetTickCount64@0
__imp__GetTickCount64@0: dd _GetTickCount64@0

global __imp__GetTimeZoneInformationForYear@12
__imp__GetTimeZoneInformationForYear@12: dd _bun_GetTimeZoneInformationForYear@12

global __imp__GetUserDefaultUILanguage@0
__imp__GetUserDefaultUILanguage@0: dd _GetUserDefaultUILanguage@0

global __imp__InitOnceBeginInitialize@16
__imp__InitOnceBeginInitialize@16: dd _InitOnceBeginInitialize@16

global __imp__InitOnceComplete@12
__imp__InitOnceComplete@12: dd _InitOnceComplete@12

global __imp__InitOnceExecuteOnce@16
__imp__InitOnceExecuteOnce@16: dd _InitOnceExecuteOnce@16

global __imp__InitializeConditionVariable@4
__imp__InitializeConditionVariable@4: dd _InitializeConditionVariable@4

global __imp__InitializeCriticalSectionEx@12
__imp__InitializeCriticalSectionEx@12: dd _InitializeCriticalSectionEx@12

global __imp__InitializeProcThreadAttributeList@16
__imp__InitializeProcThreadAttributeList@16: dd _InitializeProcThreadAttributeList@16

global __imp__InitializeSRWLock@4
__imp__InitializeSRWLock@4: dd _InitializeSRWLock@4

global __imp__InterlockedFlushSList@4
__imp__InterlockedFlushSList@4: dd _InterlockedFlushSList@4

global __imp__K32EnumProcessModules@16
__imp__K32EnumProcessModules@16: dd _K32EnumProcessModules@16

global __imp__K32GetModuleBaseNameW@16
__imp__K32GetModuleBaseNameW@16: dd _K32GetModuleBaseNameW@16

global __imp__K32GetModuleFileNameExW@16
__imp__K32GetModuleFileNameExW@16: dd _K32GetModuleFileNameExW@16

global __imp__K32GetProcessMemoryInfo@12
__imp__K32GetProcessMemoryInfo@12: dd _K32GetProcessMemoryInfo@12

global __imp__K32QueryWorkingSet@12
__imp__K32QueryWorkingSet@12: dd _K32QueryWorkingSet@12

global __imp__LCMapStringEx@36
__imp__LCMapStringEx@36: dd _bun_LCMapStringEx@36

global __imp__LCIDToLocaleName@16
__imp__LCIDToLocaleName@16: dd _LCIDToLocaleName@16

global __imp__LocaleNameToLCID@8
__imp__LocaleNameToLCID@8: dd _LocaleNameToLCID@8

global __imp__ResolveLocaleName@12
__imp__ResolveLocaleName@12: dd _ResolveLocaleName@12

global __imp__NeedCurrentDirectoryForExePathW@4
__imp__NeedCurrentDirectoryForExePathW@4: dd _NeedCurrentDirectoryForExePathW@4

global __imp__QueryMemoryResourceNotification@8
__imp__QueryMemoryResourceNotification@8: dd _QueryMemoryResourceNotification@8

global __imp__ReOpenFile@16
__imp__ReOpenFile@16: dd _ReOpenFile@16

global __imp__ReleaseSRWLockExclusive@4
__imp__ReleaseSRWLockExclusive@4: dd _ReleaseSRWLockExclusive@4

global __imp__ReleaseSRWLockShared@4
__imp__ReleaseSRWLockShared@4: dd _ReleaseSRWLockShared@4

global __imp__ResizePseudoConsole@8
__imp__ResizePseudoConsole@8: dd _ResizePseudoConsole@8

global __imp__SetFileCompletionNotificationModes@8
__imp__SetFileCompletionNotificationModes@8: dd _SetFileCompletionNotificationModes@8

global __imp__SetFileInformationByHandle@16
__imp__SetFileInformationByHandle@16: dd _SetFileInformationByHandle@16

global __imp__SetInformationJobObject@16
__imp__SetInformationJobObject@16: dd _SetInformationJobObject@16

global __imp__SetThreadpoolTimer@20
__imp__SetThreadpoolTimer@20: dd _bun_SetThreadpoolTimer@20

global __imp__SetThreadpoolTimer@16
__imp__SetThreadpoolTimer@16: dd _bun_SetThreadpoolTimer4@16

global __imp__SetThreadpoolWait@12
__imp__SetThreadpoolWait@12: dd _SetThreadpoolWait@12

global __imp__SleepConditionVariableCS@12
__imp__SleepConditionVariableCS@12: dd _SleepConditionVariableCS@12

global __imp__SleepConditionVariableSRW@16
__imp__SleepConditionVariableSRW@16: dd _SleepConditionVariableSRW@16

global __imp__TryAcquireSRWLockExclusive@4
__imp__TryAcquireSRWLockExclusive@4: dd _TryAcquireSRWLockExclusive@4

global __imp__TryAcquireSRWLockShared@4
__imp__TryAcquireSRWLockShared@4: dd _TryAcquireSRWLockShared@4

global __imp__UpdateProcThreadAttribute@28
__imp__UpdateProcThreadAttribute@28: dd _UpdateProcThreadAttribute@28

global __imp__WakeAllConditionVariable@4
__imp__WakeAllConditionVariable@4: dd _WakeAllConditionVariable@4

global __imp__WakeConditionVariable@4
__imp__WakeConditionVariable@4: dd _WakeConditionVariable@4

global __imp__WaitForThreadpoolTimerCallbacks@8
__imp__WaitForThreadpoolTimerCallbacks@8: dd _WaitForThreadpoolTimerCallbacks@8

global __imp__inet_pton@12
__imp__inet_pton@12: dd _inet_pton@12

section .text

global _SetThreadpoolTimer@20
_SetThreadpoolTimer@20:
    jmp _bun_SetThreadpoolTimer@20

global _SetThreadpoolTimer@16
_SetThreadpoolTimer@16:
    jmp _bun_SetThreadpoolTimer4@16

global _GetTimeZoneInformationForYear@12
_GetTimeZoneInformationForYear@12:
    jmp _bun_GetTimeZoneInformationForYear@12

global _CompareStringEx@36
_CompareStringEx@36:
    jmp _bun_CompareStringEx@36

global _LCMapStringEx@36
_LCMapStringEx@36:
    jmp _bun_LCMapStringEx@36

global _CreatePseudoConsole@20
_CreatePseudoConsole@20:
    jmp _bun_CreatePseudoConsole@20

global _CreateMemoryResourceNotification@4
_CreateMemoryResourceNotification@4:
    jmp _bun_CreateMemoryResourceNotification@4
