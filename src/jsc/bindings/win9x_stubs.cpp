// Win9x i586 stubs for missing WebKit/JSC symbols.
// extern "C" adds leading underscore on x86 MSVC, so names match
// what Rust's extern "C" declarations expect.
//
// API set stubs (WaitOnAddress, WakeByAddress*) live in
// win9x_apiset_stubs.cpp (compiled without PCH to avoid conflicts
// with synchapi.h).

extern "C" {

// JSC LLInt interpreter begin/end markers (Rust: extern "C" static u8).
char jsc_llint_begin[1] = {0};
char jsc_llint_end[1] = {0};

// Bun-specific JSC Wasm streaming addition (Rust: extern "C" fn).
void JSC__Wasm__StreamingCompiler__addBytes() {}

}
