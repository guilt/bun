# Building Bun for Windows 9x (i586)

This guide documents how to cross-compile Bun for i586 (32-bit x86) Windows 9x/2000/XP from a modern x64 Windows host using Visual Studio's LLVM toolchain.

## Prerequisites

The win9x build cross-compiles on a modern x64 Windows host. Everything in this
list must be reachable from the VS developer shell / `PATH` when running
`bun run build`. Tools under **`%EXTDEV%`** and **`%PATH%` shell aliases**
(`py.cmd`, git, rustup) are the "managed" toolchain. Anything installed
machine-wide outside those locations is a deliberate one-off and is called out
below so a fresh machine can be set up.

### Managed (via `%EXTDEV%` or shell aliases)

- **Rust `rust9x` cross toolchain** — lives in `%EXTDEV%\Rust9x\<arch>` and is
  linked into rustup via an NTFS junction (`~\.rustup\toolchains\rust9x`).
  This is the forked Rust that targets i586/9x.
- **Python 3** — `%EXTDEV%\Python38\` and a `py.cmd` shim in
  `%EXTDEV%\Bin\py.cmd` (tracked copy at `misctools\py.cmd`). The shim is
  version-aware (`py.cmd -3`, `py.cmd -2`, `py.cmd -3.8`, ...), honors the
  AUTOEXEC.CMD `PYTHON3_HOME` / `PYTHON2_HOME` / `PYTHON_HOME` and
  `PYTHON3_VERSION` / `PYTHON2_VERSION` / `PYTHON_VERSION` variables (no
  hardcoded paths), and is Windows 9x/2000/XP-safe (`%~$PATH:1` lookup
  instead of `where`). Used for the ICU data build (`icudt`) and for
  `misctools\pe_disable_aslr.py`.
- **Git for Windows** — `C:\Program Files\Git` (provides `perl` for ninja,
  `patch`, `git`). Git's env sets `MAKEFLAGS=j23`, which `build-icu.ps1`
  removes at runtime (see Build Environment Variables below).
- **Host Bun** — `C:\Users\<user>\.Bun\bin\bun.exe`. The build
  runner (`bun run build`) and `scripts/` are TypeScript run by **host Bun**
  (not Node.js). You do not need Node.js on the host for the win9x build.

### Machine-wide installs (outside `%EXTDEV%` — must be present explicitly)

- **Visual Studio 2022 Community** — `C:\Program Files\Microsoft Visual
  Studio\2022\Community`. Provides the C/C++ toolchain (MSVC `cl.exe`,
  Windows SDK), the bundled CMake/Ninja, and the x64 developer shell the
  build auto-re-execs into (`VSINSTALLDIR`).
- **LLVM 18+** — `C:\Program Files\LLVM` (provides `clang-cl`, `lld-link`,
  `llvm-lib`, `llvm-symbolizer`). The build auto-detects LLVM: it honors the
  AUTOEXEC-style env vars `LLVM_HOME` (a dir containing `bin\`), `LLVM_PATH`
  (already the `bin` dir), or `LLVM_ROOT` / `EXTDEV` (a root with
  `LLVM_ROOT\<arch>\bin` or `LLVM_ROOT\bin`), falling back to
  `%ProgramFiles%`/`%ProgramFiles(x86)%` then `PATH`. If tools still aren't
  found outside a VS dev shell, prepend `C:\Program Files\LLVM\bin` to `PATH`
  or set one of the env vars above.
- **CMake** — `C:\Program Files\CMake` (used by the WebKit and ICU `local`
  dependency builds).
- **rustup** itself — `~\.cargo` / `~\.rustup` (the host manager that links
  the `rust9x` toolchain; also owns the `nightly` toolchain you can keep for
  `-Zbuild-std`).

### Sources (not installed binaries)

- WebKit source at `vendor/WebKit/` (clone `oven-sh/WebKit`).
- ICU 78.3 source at `vendor/icu/` or `%BUN_ICU_PATH%` (the `local` ICU
  dependency builds it from source as static `/MT` libs).

## Toolchain Setup

```powershell
# Install LLVM (provides clang-cl, lld-link, llvm-lib)
# Install from https://github.com/llvm/llvm-project/releases

# Install Rust nightly
rustup toolchain install nightly-2026-07-20
rustup component add rust-src --toolchain nightly-2026-07-20

# Register the rust9x cross-compiler (the Rust fork that targets 9x/XP).
# rustup toolchain link creates a symlink; the build machine instead uses
# NTFS junctions so the toolchain can be relocated freely:
#   rust9x      -> %EXTDEV%\Rust9x\<arch>  (distributed toolchain)
#   rust9x-msvc -> D:\WS\Rust9x-Rust\build\x86_64-pc-windows-msvc\stage2
# %EXTDEV% / $env:EXTDEV is the "External Development Tools" root (D:\WS\EXTDEV),
# a shared tree of third-party toolchains (GCC/MinGW, Python, CMake, the rust9x
# distribution). Arch-specific tools live in an arch-qualified subfolder:
# the rust9x dist is at %EXTDEV%\Rust9x\<arch>, where <arch> is the HOST's
# $env:PROCESSOR_ARCHITECTURE (AMD64, ARM64, ...).
rustup toolchain link rust9x $env:EXTDEV/Rust9x/$env:PROCESSOR_ARCHITECTURE

# Clone WebKit source
git clone https://github.com/oven-sh/WebKit vendor/WebKit/
```

If the `rust9x` toolchain junction breaks (its target was moved), re-create it:

```powershell
$link = "$env:USERPROFILE\.rustup\toolchains\rust9x"
Remove-Item -LiteralPath $link -Force -Recurse
New-Item -ItemType Junction -Path $link -Target "$env:EXTDEV\Rust9x\$env:PROCESSOR_ARCHITECTURE"
rustc +rust9x --target i586-rust9x-windows-msvc --print cfg
```

## Build Configuration

Two profiles are available for Win9x builds:

| Profile | Type | LTO | Debug Info |
|---------|------|-----|-----------|
| `win9x-debug` | Debug | Off | Full PDB |
| `win9x-release` | Release | Off | Limited |

### Profile Details

```ts
// scripts/build/profiles.ts
"win9x-debug": {
    buildType: "Debug",
    os: "windows",
    arch: "i586",
    webkit: "local",   // Uses vendor/WebKit/ source
    icu: "local",      // Builds ICU 78.3 from vendor/icu source (static /MT)
},

"win9x-release": {
    buildType: "Release",
    os: "windows",
    arch: "i586",
    webkit: "local",
    icu: "local",
    lto: false,
},
```

## Build Steps

### 1. Apply rust-src Patches

The nightly toolchain's rust-src must be patched to avoid linking Windows 8+ API set DLLs:

```powershell
# Patch standard library to skip api-ms-win-core-synch-l1-2-0 link for vendor = "rust9x"
# Edit: <rust-sysroot>/lib/rustlib/src/rust/library/std/src/sys/pal/windows/c.rs
#   Change #[cfg(not(target_vendor = "win7"))] to #[cfg(not(any(target_vendor = "win7", target_vendor = "rust9x")))]
#   Change #[cfg(target_vendor = "win7")] to #[cfg(any(target_vendor = "win7", target_vendor = "rust9x"))] for compat macros

# Edit: <rust-sysroot>/lib/rustlib/src/rust/library/std/src/sys/pal/windows/compat.rs
#   Change #[cfg(target_vendor = "win7")] on macro_rules! compat_fn_optional to #[cfg(any(target_vendor = "win7", target_vendor = "rust9x"))]
```

### 2. Patch getrandom crate (cargo registry)

```powershell
# Edit: ~/.cargo/registry/src/*/getrandom-0.4.2/src/backends/windows.rs
#   Add #[cfg(target_vendor = "rust9x")] block with RtlGenRandom from advapi32
#   Gate existing ProcessPrng block with #[cfg(not(target_vendor = "rust9x"))]
```

### 3. Configure and Build WebKit (first time only)

The build system builds WebKit from source with C Loop (no JIT) for i586:

- Targets: WTF, bmalloc, JavaScriptCore
- JIT disabled, C Loop enabled
- Mimalloc for allocations (patched to not link bcrypt.lib)
- Static CRT (/MTd or /MT)
- ICU built from source via the `icu` dependency (`scripts/build/deps/icu.ts`,
  `build-icu.ps1`) as static `/MT` libs
- CMake target: `i586-pc-windows-msvc`

WebKit is built automatically as a dependency when running the main build.

### 4. Build Bun

```powershell
# Full build (configure + ninja)
bun run build --profile=win9x-debug

# Or step-by-step:
bun run build --profile=win9x-debug --configure-only  # Generate build.ninja
ninja -C build/debug                                    # Run the build
```

The configure step must run inside a Visual Studio developer shell (x64). The build script auto-re-execs if `VSINSTALLDIR` is missing.

### 4b. Disable ASLR before running

The win9x binary is linked with ASLR (`DllCharacteristics = 0x8140`). Load with
a fixed base for the C Loop to work reliably and to reproduce a stable debug
address. Clear the ASLR flags and pin the 32-bit base to `0x400000`:

```powershell
python misctools\pe_disable_aslr.py build\debug\bun-debug.exe
# DllCharacteristics 0x8140 -> 0x8100 ; ImageBase 0x400000
```

Re-run whenever the exe is rebuilt (ninja overwrites the header).

### 5. Deploy to XP

```powershell
# Copy binary to target (ICU is statically linked — no ICU DLLs; the Win8+
# API set stubs such as ProcessPrng/RtlWaitOnAddress are compiled directly
# into the exe via win9x_apiset_stubs.cpp, so no companion DLL is needed)
scp build/debug/bun-debug.exe user@xp-machine:C:/Bun/
```

## Architecture Details

### Rust Target Spec

The custom target spec at `vendor-patches/i586-rust9x-windows-gnu.json`:

```json
{
    "arch": "x86",
    "cpu": "pentium4",
    "env": "msvc",
    "is-like-msvc": true,
    "llvm-target": "i586-pc-windows-msvc",
    "vendor": "rust9x",
    ...
}
```

Key points:
- Uses MSVC ABI (`is-like-msvc: true`) with LLVM tools (lld-link, llvm-lib)
- `vendor: "rust9x"` activates compat patches in std library and getrandom
- Targets `pentium4` for SSE2 support
- Uses `no-default-libraries: false`

### WebKit Configuration

i586 WebKit cmake flags in `scripts/build/deps/webkit.ts`:

```
ENABLE_JIT=OFF
ENABLE_FTL_JIT=OFF
ENABLE_C_LOOP=ON
OFFLINE_ASM_BACKEND=C_LOOP
USE_MIMALLOC=ON
USE_SYSTEM_MALLOC=OFF
ENABLE_REMOTE_INSPECTOR=OFF
USE_BUN_JSC_ADDITIONS=ON
CMAKE_C_COMPILER_TARGET=i586-pc-windows-msvc
CMAKE_CXX_COMPILER_TARGET=i586-pc-windows-msvc
CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebug  (or MultiThreaded)
```

## Fixes Applied

### XP/9x API Set Stubs

Windows 8+ API set DLLs (`api-ms-win-core-synch-l1-2-0.dll`, `bcryptprimitives.dll`) are not available on XP. All their symbols are compiled directly into the exe by the following file — no companion stub DLL is needed:

- **`src/jsc/bindings/win9x_apiset_stubs.cpp`** — Standalone TU (no PCH) providing:
  - `WaitOnAddress`, `WakeByAddressAll`, `WakeByAddressSingle` (synchronization)
  - `ProcessPrng` (PRNG, forwards to `SystemFunction036` from advapi32)
  - `BCryptGenRandom` (PRNG, forwards to `SystemFunction036`)
  - `RtlWaitOnAddress`, `RtlWakeAddressAll`, `RtlWakeAddressSingle` (ntdll sync, returns STATUS_TIMEOUT)
  - `RtlRestoreContext` (ntdll exception handling, no-op)
  - `RtlExitUserProcess` (ntdll process exit, forwards to ExitProcess)
  - Alternatename pragmas for `__imp__` symbols

- **`src/jsc/bindings/win9x_stubs.cpp`** — JSC/WebKit symbol stubs:
  - `jsc_llint_begin`, `jsc_llint_end` (LLInt markers)
  - `JSC__Wasm__StreamingCompiler__addBytes` (Wasm streaming)

- **`src/jsc/bindings/xp_compat.cpp`** — emulations for Vista+ APIs that are
  imported by the CRT/WebKit, so `bun-debug.exe` has no Vista+ imports:
  - `AcquireSRWLock*`/`ReleaseSRWLock*`/`TryAcquireSRWLock*` → backed by a
    lazily-initialized 256-slot `CRITICAL_SECTION` pool (SRW is 4 bytes on x86;
    a real `CRITICAL_SECTION` doesn't fit, hence the pool)
  - `InitOnceBeginInitialize`/`InitOnceComplete`/`InitOnceExecuteOnce` →
    state machine (0/1/2) + 256-slot context cache, correct blocking semantics
  - `GetCurrentThreadStackLimits` → reads the TEB (FS:[8]/FS:[4]); must be
    `VOID __stdcall` per the SDK decl
  - `GetFinalPathNameByHandleW` → passthrough to real kernel32 when available,
    else `NtQueryObject` + `QueryDosDeviceW` emulation
  - `GetTickCount64`, `GetSystemTimePreciseAsFileTime`, `inet_pton`,
    `InitializeConditionVariable`, `CreateEventEx`, `CreateFile2`, etc.
  - `GetAddrInfoW`/`FreeAddrInfoW` → serviced over XP's ANSI
    `getaddrinfo`/`freeaddrinfo` (libuv imports the `*W` DNS entry points,
    which are Vista+ and absent on XP → `0xC0000139` at load). The `__imp__`
    data symbols in `xp_win9x_imports.asm` kill the ws2_32 IAT entries.
  - `SHGetKnownFolderPath` → delay-loaded (SHELL32), the `dliFailGetProc` hook
    returns the E_NOTIMPL stub. **Must** be in the `/delayload:` list: without
    it SHELL32 was a hard IAT import and XP died at load.

- **`src/jsc/bindings/xp_win9x_imports.asm`** — the import-table overrides.
  Each Vista+ import is listed as an object-level data symbol
  `__imp__Foo@N` (N = x86 `__stdcall` decorated size). An object symbol beats
  an import library entry, so the linker resolves `Foo` to the stub in
  `xp_compat.cpp` instead of emitting a kernel32/ws2_32 IAT entry. A few
  `bun_`-renamed stubs additionally need `.text` jmp thunks (lld crashes
  duplicating `__imp__` when only the plain `_Foo@N` is referenced). Note
  `SetThreadpoolTimer` must be emitted as `__imp__SetThreadpoolTimer@16`
  (matching libcpmtd's own `__imp__`), not the SDK's `@20`.

> The `__imp_` override technique: `xp_win9x_imports.asm` provides a
> **data** symbol `__imp__Foo@N`; `xp_compat.cpp` provides the function body
> `Foo`. Because both the function and its `__imp_` pointer come from object
> files (not an import lib), the final exe has no IAT entry for `Foo`. Verify
> with `dumpbin /imports` — after the work, only `InterlockedPopEntrySList`
> (XP SP2+, acceptable) remains from the flagged list.

### 32-Bit Runtime Fixes

- **ZigString pointer tags on 32-bit**: bun's Rust side tags string pointers
  using the top bits — bits 60-63 on 64-bit. On 32-bit the C++ side and Rust
  side (`bun_alloc/lib.rs` `zs_tags`) agree on the layout but it **does not
  mirror the 64-bit shift**: the STATIC pointer-tag is unused on 32-bit
  (static strings use `BunString::S_REF_COUNT_FLAG_IS_STATIC_STRING` instead)
  so bit 28 stays a **real address bit**, and the tags live in bits **29-31**
  (utf8=29, global=30, utf16=31). `untag`, `isTaggedUTF8Ptr`,
  `isTaggedUTF16Ptr`, `isTaggedExternalPtr`, and `taggedUTF16Ptr` are
  `#if CPU(ADDRESS64)`-split.
- **ZigString untag width on 32-bit**: `bun_alloc/lib.rs`'s 32-bit
  `ZS_UNTAG_MASK` must keep the low 29 bits (clear only the 29-31 tag bits),
  and `helpers.h`'s 32-bit `untag` must clear only bits 29/30/31. Both now keep
  bit 28 as address. The previous mask `(1 << 25) - 1` (32 MB) truncated heap
  strings, and `(1 << 28) - 1`-style bit-28 clearing corrupted any pointer
  ≥ 268 MB — win9x JS string buffers sit at ~330 MB, so node:path
  (join/basename/extname) returned garbage and the `bun test` runner crashed
  on load.
- **Missing-property sentinel under JSVALUE32_64 (`ObjectBindings.cpp`)**:
  `getIfPropertyExistsPrototypePollutionMitigationUnsafe` returned `JSValue()`
  (empty) for a missing property. On 64-bit JSVALUE64 an empty `JSValue()`
  encodes to a distinct "empty" that the Rust side reads as
  `ValueDeleted`-style missing; but on JSVALUE32_64 a `JSValue()`
  (tag=EmptyValue, payload=..., encoded as `0x…000`) collapses into the same
  bit pattern as the "threw exception" sentinel, so every options-object read
  — `fs.mkdirSync(..., { recursive: true })`, `fs.readFileSync(..., "utf8")`,
  `bun test`'s options — aborted with "Expected an exception to be thrown".
  It now returns the `HashTableDeletedValue` sentinel (encodes to
  `0x4` = `PROPERTY_DOES_NOT_EXIST`) before checking the prototype chain, on
  all platforms.
- **`bun_jsc::JSValue` must be 64-bit on 32-bit targets**: JSC uses
  **JSVALUE32_64** even on 32-bit, so `EncodedJSValue` is 8 bytes.
  `JSValue` was `#[repr(transparent)] struct JSValue(pub usize, …)` — 4 bytes
  on i586 — which misaligned every FFI struct embedding a JSValue
  (`ZigErrorType`/`ErrorableString`, `ResolvedSource`, host-fn args) and
  corrupted values: the module-loader error came back as a garbage double
  (`0x42830100000008`) and `throwException` threw uninitialized memory.
  `JSValue` now stores a `u64` and the `JSValue must be 64 bits` assert is
  unconditional. (`src/jsc/JSValue.rs`)
- **JSVALUE32_64 encoding in the Rust side**: the Rust `JSValue` helpers
  assumed JSVALUE64 (undefined=0xa, int32 via the `0xfffe_0000_0000_0000`
  tag, doubles via the `+2^49` `DoubleEncodeOffset`). On 32-bit the encoding
  is `(tag << 32) | payload` with NaN-space tags — Int32=0xffffffff,
  Boolean=0xfffffffe, Null=0xfffffffd, Undefined=0xfffffffc, Cell=0xfffffffb,
  EmptyValue=0xfffffff9 — and doubles are stored as their raw bit pattern.
  `JSValue.rs` (tag constants, `is_undefined`/`is_null`/`is_boolean`/
  `is_cell`/`is_int32`/`is_number`/`is_double`, `js_boolean`,
  `js_number_from_int32`, `js_double_number`, `as_double`, `ZERO`/`is_empty`)
  and `DecodedJSValue.rs` (`is_cell`/`as_cell`) now branch on
  `target_pointer_width`. The `ffi::{NUMBER_TAG, NOT_CELL_MASK, DOUBLE_ENCODE_*}`
  constants are 64-bit-only.
- **CallFrame slot offsets**: `JSC::CallFrameSlot` starts at
  `CallerFrameAndPC::sizeInRegisters` = `(2 * sizeof(void*)) / sizeof(Register)`.
  On 64-bit that is 2, but on 32-bit JSVALUE32_64 a pointer is 4 bytes while a
  `Register` is 8, so `sizeInRegisters` is **1** and every slot shifts down by
  one. The hardcoded offsets read the argument count from the `this` slot (a
  cell pointer → a ~320M "argument count" → `slice::from_raw_parts` UB panic
  on the first host call). `CallFrame.rs` now computes the offsets the same
  way JSC does (`REGISTERS_FOR_CALLER_FRAME_AND_PC`).
- **`xp_compat.cpp` condition-variable fixes**: `SleepConditionVariableSRW`
  must map the SRWLOCK through `xp_srwCS()` (the 256-slot `CRITICAL_SECTION`
  pool) instead of casting the lock address directly — passing a pool index
  as a `PCRITICAL_SECTION` made `EnterCriticalSection` write at address 0x20.
  And Windows CONDITION_VARIABLEs are zero-initialized and **self-initialize
  on first use** (WTF's `ThreadCondition` never calls
  `InitializeConditionVariable`), so the emulation now lazy-creates its event
  with `InterlockedCompareExchange` instead of requiring an explicit init.
- **JIT/WASM options for i586**: WebKit forces `useWasmIPInt`/`useBBQJIT` off
  on non-x86-64/ARM64, so enabling WASM makes `assertOptionsAreCoherent()`
  crash at startup. `ZigGlobalObject.cpp`'s `JSC::initialize` callback now
  sets `useWasm()=false`, `useJIT()=false`, `useConcurrentJIT()=false` on
  32-bit (LLInt interpreter only).
- **zlib functable on i586**: `functable.c` only assigns the generic C
  fallbacks on x86_64 or with `WITH_ALL_FALLBACKS`. On 32-bit x86 `adler32` /
  `adler32_fold_copy` stayed NULL, so `init_functable()` aborted
  ("Zlib-ng functable failed initialization!") the moment zlib ran (gzip,
  deflate, `node:zlib`). `scripts/build/deps/zlib.ts` now sets
  `WITH_ALL_FALLBACKS` for `cfg.x86`.

### Rust Calling Convention

Changed `extern "C"` to `extern "system"` for Windows API functions in Rust:
- `GetUserNameW` → moved to advapi32 `extern "system"` block
- `SetStdHandle`, `GetConsoleOutputCP`, `GetConsoleCP` → moved to kernel32 `extern "system"` block
- `SetEnvironmentVariableW` → changed to `extern "system"` at call site
- `htons`/`ntohs` → changed to `extern "system"` (stdcall on Windows)

### Rust FFI Declarations Moved Out of #[link] Blocks

To prevent linker from creating imports from ntdll.dll/kernel32.dll for
functions that don't exist on XP, these declarations were moved from
`#[link(name = "...")]` blocks to standalone `unsafe extern "system"` blocks:

- `RtlWaitOnAddress`, `RtlWakeAddressAll`, `RtlWakeAddressSingle`
- `RtlExitUserProcess`
- `GetHostNameW` (replaced with ANSI gethostname + UTF-16 conversion)

### x86 Import Library Paths

Added automatic detection of x86 MSVC CRT and Windows SDK import library
paths from VS dev shell environment variables (`VCToolsInstallDir`,
`WindowsSdkDir`, `WindowsSDKLibVersion`). These are passed as `/libpath:`
flags to lld-link.

### Linker Flags (flags.ts)

For x86 Win9x targets, the following flags are added:
- `/subsystem:console,5.01` — XP subsystem version
- `/NODEFAULTLIB:synchronization.lib` — exclude Win8+ API set
- `/NODEFAULTLIB:bcrypt.lib` — exclude Win8+ BCrypt
- `/FORCE:MULTIPLE` — allow duplicate symbols (stub wins)
- `/alternatename:_RtlRestoreContext@8=_RtlRestoreContext`

### WebKit Vendor Patches

Four patches in `vendor-patches/WebKit/`:
- `DOMWrapperWorld.h.patch` — strip WebCore DOM wrapper deps
- `JSDOMConstructorBase.h.patch` — strip constructor base deps
- `JSDOMWrapper.h.patch` — strip wrapper deps
- `mimalloc-no-bcrypt-link.patch` — remove bcrypt.lib from mimalloc link list

Applied automatically during build via the `dep_patch_local` ninja rule.

### Symbol Export

- `src/symbols.x86.def` — x86-specific symbol export list (no x64 v8 mangled symbols)
- Used automatically when `cfg.x86` is true

### Node.js Hostname (node_os.rs)

Replaced `GetHostNameW` (Windows 10+) call with ANSI `gethostname`
from ws2_32 (available on all Windows versions) + string conversion.

### Compile-Time Patches

- **getrandom v0.4.2 (`~/.cargo/registry/...`)**: Uses `RtlGenRandom` from
  advapi32 for `target_vendor = "rust9x"` instead of `ProcessPrng` from
  bcryptprimitives.dll (which is statically stubbed in `win9x_apiset_stubs.cpp`).
- **nightly std library (`c.rs`)**: Skips `#[link(name = "api-ms-win-core-synch-l1-2-0")]`
  and uses dynamic loading macros for `target_vendor = "rust9x"`.
- **nightly std library (`compat.rs`)**: Exposes `compat_fn_optional!` macro
  for `target_vendor = "rust9x"`.

## Known Issues

### Current Status

As of this writing the i586 debug build **loads and runs on the XP VM**
(`--revision`, eval, promises, timers, `async/await`, `node:fs`) with a clean
XP-compatible import table (verified via `dumpbin /imports`):
- ICU is statically linked (static `/MT` `icuuc`/`icuin`/`icudt`), so the import
  table is just `KERNEL32.dll`/`ntdll.dll`/`dbghelp.dll`.
- The Vista+ kernel32 APIs ICU needs (`GetCurrencyFormatEx`, `GetDateFormatEx`,
  `GetNumberFormatEx`, `GetTimeFormatEx`, `LocaleNameToLCID`, `LCIDToLocaleName`,
  `ResolveLocaleName`, `GetDynamicTimeZoneInformation`) are overridden to XP
  polyfills via `src/jsc/bindings/xp_compat.cpp` +
  `src/jsc/bindings/xp_win9x_imports.asm` (`__imp__` data symbols beat the
  kernel32 import library, so no IAT entry / no load failure on XP).
- The bundled built-in JS (`node:fs`, `node:zlib`, ...) is **embedded** into the
  binary (win9x sets `embeddedModules`), so it doesn't depend on the build dir —
  the win9x build also bundles production (minified) builtins so the embedded
  byte arrays stay under clang's constexpr limit
  (`/clang:-fconstexpr-steps=6000000`).

Verified working end-to-end on the host (win9x debug build):
- `bun test` runs a test suite (exit 0), `node:path` (join/basename/extname),
  dynamic `import()`, `async/await`, promises, timers, `Bun.sleep`,
  `TextEncoder`/`TextDecoder`, and recursive `fs.mkdirSync({ recursive: true })`
  with fs options objects (all previously crashing — fixed by the
  JSVALUE32_64 sentinel + ZigString tag work above).
- Plain-JS feature harnesses pass (7/7 and 14/14 assertions).
- UTF-8 source files load correctly (multi-byte literals survive the
  transpiler; an earlier `é` → U+FFFD report was a PowerShell `Set-Content`
  ANSI-encoding artifact, not a bun bug).

### Binary Boot on XP

The binary requires the following files alongside it on XP:
- `dbghelp.dll` (may not be on XP by default — ship a copy; use the XP copy,
  not the Win10 one — the Win10 `dbghelp.dll` imports `api-ms-win-crt-private`)

ICU is statically linked (`icuuc.lib`/`icuin.lib`/`icudt.lib`, static `/MT`) so
no `icuuc78.dll`/`icuin78.dll`/`icudt78.dll` are deployed. The built-in JS is
embedded, so no `js/` directory is needed either. The Win8+ API set symbols
(`ProcessPrng`, `BCryptGenRandom`, `RtlWaitOnAddress`, `RtlWakeAddress*`,
`RtlExitUserProcess`) are compiled into the exe by `win9x_apiset_stubs.cpp`,
so **no companion stub DLL is required**. Deploy:
`scp -O bun-debug.exe <user>@KVK-Retro-PC.local:Bun/`.

### Build Environment Variables

| Variable | Purpose |
|----------|---------|
| `BUN_WEBKIT_PATH` | Path to a WebKit checkout (default `vendor/WebKit/`); lets several worktrees share one clone. |
| `BUN_ICU_PATH` | Path to an ICU 78.3 source root (default `vendor/icu/icu4c/source`); mirrors `BUN_WEBKIT_PATH`. |
| `EXTDEV` | External-tools root (`D:\WS\EXTDEV`); rust9x toolchain junction target. |
| `MAKEFLAGS` | **Removed** by `build-icu.ps1` at runtime — Git's env sets `j23`, which NMAKE rejects (`U1065`). |
| `PATH` | Needs `py` on PATH (Python 3 for the ICU data build; the `py.cmd` shim in `%EXTDEV%\Bin` selects the interpreter by `-3`/`-3.x`/`-2`/`-2.x` selector or the AUTOEXEC `PYTHON*_HOME`/`PYTHON*_VERSION` variables). If clang/llvm tools fail to be found outside the VS dev shell, prepend `C:\Program Files\LLVM\bin`. **You no longer need to add Git's `usr\bin` to `PATH` for perl** — the build auto-detects it (see Reproducibility below). |
| `VSINSTALLDIR` | Set by running inside a VS developer shell (x64). `scripts/build.ts` auto-re-execs if unset. |

## Reproducibility

A fresh machine should be able to run `bun run build --profile=win9x-debug`
with no manual `PATH` edits or hidden prerequisites. The build system handles
the following automatically:

### Perl auto-detection (LUT codegen + WebKit)

Several codegen steps shell out to **perl** (`create-hash-table.ts` parses
`@begin … @end` blocks into JSC `HashTableValue` arrays), and WebKit's cmake
runs `find_package(Perl)`. Historically this required Git for Windows'
`usr\bin` to be manually added to `PATH`. Now:

- **`findPerl()`** (`scripts/build/tools.ts`) searches, in order:
  `%PERL5_HOME%`/`%PERL_HOME%`'s `bin`, `%EXTDEV%\Perl%PERL5_VERSION%\bin`,
  `%EXTDEV%\Perl\bin`, Git for Windows' bundled `perl` at
  `%ProgramFiles%\Git\usr\bin`, `%ProgramFiles(x86)%\Git\usr\bin`, and
  `%LocalAppData%\Programs\Git\usr\bin` (per-user install), and finally `PATH`.
- **LUT codegen** (`scripts/build/codegen.ts` `registerCodegenRules`): the
  `codegen` rule prepends the resolved perl's bin dir to `PATH`, so the
  build-time `perl` invocation resolves even when Git's perl isn't on the
  machine's `PATH`.
- **WebKit cmake** (`scripts/build/deps/webkit.ts`): on Windows it passes
  `-DPERL_EXECUTABLE=<resolved perl>` to cmake, satisfying `find_package(Perl)`
  deterministically.

### `TARGET_ARCH` for 32-bit targets (`scripts/build/codegen.ts`)

`codegenTarget()` previously returned `"arm64"` for **any** non-x64 target,
so an i586/x86 build baked `process.arch = "arm64"` into the bundled built-ins.
It now returns `"x86"` when `cfg.x86` is set. That exposed two pre-existing
`os.ts` hardcoded `$bundleError` "TODO" branches that only knew
arm64/x64 — `src/js/node/os.ts` `endianness()` and `machine()` now handle
`"x86"` (`endianness → "LE"`, `machine → "x86"`, matching Node on Windows x86).

### `.patched` stamp actually written (`scripts/build/fetch-cli.ts`)

`dep_patch_local` declared `stamps/<dep>.patched` as its output, but
`applyLocalPatches()` never wrote it — so the stamp never existed and the
ICU/WebKit patch **and** prebuild steps re-ran on **every** build. The ninja
rule now passes the stamp path (`apply-local-patches $srcdir $stamp $patches`,
`source.ts`), and `applyLocalPatches()` writes it (idempotently, via
`writeIfChanged`, keyed on the patch-identity hash) after applying patches.
Editing a patch bumps the stamp and correctly invalidates downstream.

### ICU file-copy retry (`scripts/build/deps/icu/build-icu.ps1`)

`Copy-Item` can fail transiently with "**user-mapped section open**" when AV
scanning / the search indexer / a leftover ICU tool has a file memory-mapped.
The header and library copies now go through `Copy-IcuFile`, which retries (5
attempts, 2 s apart) before failing the build.

## ICU local build (mirrors local WebKit)

The `icu` dependency has `prebuilt` and `local` modes (`cfg.icu`), set via the
profile or `--icu=local`. The win9x profiles use `local` because the prebuilt
ICU DLLs are `/MD` (Win10 runtimes, absent on XP).

- **prebuilt**: downloads `icu4c-78.3-<arch>-MSVC2022.zip`.
- **local**: builds ICU 78.3 from `vendor/icu` (clone `unicode-org/icu`,
  `release-78.3`) or `$BUN_ICU_PATH`, via `scripts/build/deps/icu/build-icu.ps1`
  (msbuild). Vendor patches in `vendor-patches/icu/` are applied by the dep
  pipeline (`git apply`): `testdata.mak` `TESTDATATMP` quoting, `makedata.mak`
  `--filter_file` removal, and skipping the testdata target (bun needs no ICU
  test data).

The build: a stub `icudt78.dll` (from `stubdata`) bootstraps the data tools
(gencnval/genrb need `icuuc78d.dll` → `icudt78.dll`), then the data is built
as a DLL (`-m dll`), rebuilt as static (`-m static` → `sicudt.lib`), and
common/i18n are rebuilt static `/MT`. Outputs land in
`build/<profile>/deps/icu/lib/{icudt,icuin,icuuc}.lib` and WebKit's cmake reads
`ICU_ROOT` from there.

### Missing v8 Shim Symbols

The v8 C++ API shim in `src/jsc/bindings/v8/` compiles but uses x86 MSVC
mangled names. The Rust FFI declarations in `napi_body.rs` use x64 MSVC
mangled names. On x86, the linker can't resolve them. Fix: update
`#[link_name]` attributes with x86-specific mangled names.

### WebKit JSC Symbols

- `_JSC__Wasm__StreamingCompiler__addBytes` — Bun-specific JSC addition,
  requires WebKit rebuild with `USE_BUN_JSC_ADDITIONS=ON`
- `_jsc_llint_begin` / `_jsc_llint_end` — LLInt symbols, requires
  C Loop WebKit build

### WebKit Build Time

Building WebKit from source takes 30-60 minutes. Consider using a pre-built
WebKit from the build cache when not changing WebKit configuration.

## Troubleshooting

### Linker: "can't open 'Files\Microsoft'" or "no such file or directory: '/NOLOGO'"
Paths with spaces need proper quoting. Also ensure the VS dev shell is
NOT loaded for x86 (HostX64\x86) — use x64 shell to cross-compile.

### Linker: undefined v8::* symbols
Missing v8 shim .obj files. Ensure `src/jsc/bindings/v8/*.cpp` are included
in the C++ source list in `bun.ts`.

### Rust: "can't find crate for `core`"
Need `-Zbuild-std` with nightly toolchain and `rust-src` component installed.

### C++: "unsupported architectures" from WebKit headers
Use local WebKit build (`--webkit=local`) with C Loop enabled. Pre-built
WebKit is x64-only.

### XP: "The procedure entry point X could not be located in DLL Y"
The binary imports a function that doesn't exist on XP. Check dumpbin output
for the function and add a stub in `win9x_apiset_stubs.cpp` or move the
Rust declaration out of its `#[link]` block.
