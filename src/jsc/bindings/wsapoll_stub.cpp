// WSAPoll → select() polyfill for Windows XP. WSAPoll was introduced in
// Vista's ws2_32.dll and is missing on XP.  This file provides:
//
//   1. A delay-load failure hook that intercepts WSAPoll resolution and
//      returns a select()-based implementation.
//   2. The actual bun_wsapoll_stub function.
//
// ws2_32.dll is delay-loaded (see flags.ts /delayload:WS2_32.dll) so the
// missing export doesn't prevent process startup.  The hook fires on first
// call to any ws2_32 function, at which point we supply our own WSAPoll.
//
// Compiled with PCH (includes <windows.h> and friends).

#include <windows.h>
#include <delayimp.h>

// ── WSAPoll replacement via select() ──────────────────────────────────────

struct wsapoll_pollfd {
    unsigned int fd;
    short events;
    short revents;
};

struct wsapoll_fd_set {
    unsigned int fd_count;
    unsigned int fd_array[64];
};

struct wsapoll_timeval {
    long tv_sec;
    long tv_usec;
};

extern "C" int __stdcall bun_wsapoll_stub(struct wsapoll_pollfd* fdArray, unsigned long fds, int timeout) {
    struct wsapoll_fd_set readfds, writefds, exceptfds;
    unsigned int i;
    int max_fd = 0;

    readfds.fd_count = 0;
    writefds.fd_count = 0;
    exceptfds.fd_count = 0;

    for (i = 0; i < fds; i++) {
        unsigned int s = fdArray[i].fd;
        if (fdArray[i].events & 0x0300) {
            readfds.fd_array[readfds.fd_count++] = s;
        }
        if (fdArray[i].events & 0x0410) {
            writefds.fd_array[writefds.fd_count++] = s;
        }
        if (fdArray[i].events & 0x0008) {
            exceptfds.fd_array[exceptfds.fd_count++] = s;
        }
        if ((int)s > max_fd) max_fd = (int)s;
    }

    struct wsapoll_timeval tv;
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    int ret = select(max_fd + 1,
                     readfds.fd_count ? (fd_set*)&readfds : 0,
                     writefds.fd_count ? (fd_set*)&writefds : 0,
                     exceptfds.fd_count ? (fd_set*)&exceptfds : 0,
                     timeout >= 0 ? (const struct timeval*)&tv : 0);

    if (ret > 0) {
        for (i = 0; i < fds; i++) {
            unsigned int s = fdArray[i].fd;
            unsigned int j;
            fdArray[i].revents = 0;
            for (j = 0; j < readfds.fd_count; j++)
                if (readfds.fd_array[j] == s) { fdArray[i].revents |= 0x0301; break; }
            for (j = 0; j < writefds.fd_count; j++)
                if (writefds.fd_array[j] == s) { fdArray[i].revents |= 0x0010; break; }
        }
    }
    return ret;
}

// ── Delay-load failure hook ───────────────────────────────────────────────
//
// When a delay-loaded function from ws2_32.dll is called on XP, the
// delay-load helper calls GetProcAddress for each imported symbol.  WSAPoll
// isn't exported by XP's ws2_32.dll, so that lookup fails, triggering
// dliFailGetProc.  We intercept it here and return our polyfill.

static FARPROC WINAPI delayHook(unsigned dliNotify, PDelayLoadInfo pdli) {
    if (dliNotify == dliFailGetProc) {
        if (pdli->dlp.fImportByName &&
            pdli->dlp.szProcName &&
            strcmp(pdli->dlp.szProcName, "WSAPoll") == 0) {
            return (FARPROC)bun_wsapoll_stub;
        }
        // Also handle ordinal import (though Rust/C++ use named imports).
        if (!pdli->dlp.fImportByName &&
            pdli->dlp.dwOrdinal == 46) {  // WSAPoll ordinal in ws2_32.dll
            return (FARPROC)bun_wsapoll_stub;
        }
    }
    return 0;
}

// NOTE: delay-load hook registration is in xp_compat.cpp (comprehensive).

// ── Alternatename ───────────────────────────────────────────────────────────
// Redirect dllimport of WSAPoll from ws2_32.dll to our stub at link time.
// On x86 stdcall: __imp__WSAPoll@12 → _bun_wsapoll_stub@12.
#pragma comment(linker, "/alternatename:__imp__WSAPoll@12=_bun_wsapoll_stub@12")
