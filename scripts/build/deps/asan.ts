// i586 AddressSanitizer runtime setup for win9x builds.
//
// LLVM's clang resource dir ships only the x86_64 ASAN runtime; MSVC bundles
// the i386 one. ASAN on clang-cl requires -resource-dir to point at a resource
// dir that contains BOTH the builtin headers AND the i386 ASAN runtime libs
// (overriding the resource dir redirects the whole lookup). So this module
// mirrors the full clang resource dir into <buildDir>/clang-rt-i586 and drops
// the MSVC i386 ASAN libs in. All paths are derived from the toolchain/env —
// nothing machine-specific is hardcoded.
//
// Invoked from configure() when `cfg.windows && cfg.x86 && cfg.asan`.
import { copyFileSync, existsSync, mkdirSync, readdirSync, statSync } from "node:fs";
import { execSync } from "node:child_process";
import { dirname, join } from "node:path";
import type { Config, Toolchain } from "../config.ts";

/// Derive the LLVM root from a clang-cl path (`<root>\bin\clang-cl.exe`).
function llvmRootFromClang(clang: string): string {
  // clang-cl.exe -> bin -> root
  return dirname(dirname(clang));
}

/// Find `<llvmRoot>\lib\clang\<version>` (the resource dir). Chooses the
/// highest version dir if several exist.
function findClangResourceDir(llvmRoot: string): string {
  const clangDir = join(llvmRoot, "lib", "clang");
  if (!existsSync(clangDir)) throw new Error(`no clang resource root at ${clangDir}`);
  const vers = readdirSync(clangDir)
    .filter((v) => statSync(join(clangDir, v)).isDirectory())
    .sort((a, b) => (parseInt(a, 10) || 0) - (parseInt(b, 10) || 0));
  if (!vers.length) throw new Error(`no versioned clang resource dirs under ${clangDir}`);
  return join(clangDir, vers[vers.length - 1]!);
}

/// MSVC tools root: prefer the dev-shell VCToolsInstallDir, else VSINSTALLDIR.
function msvcRoot(): string {
  const vct = process.env.VCToolsInstallDir;
  if (vct && existsSync(vct)) return vct;
  const vs = process.env.VSINSTALLDIR;
  if (vs) {
    const base = join(vs, "VC", "Tools", "MSVC");
    if (existsSync(base)) {
      const vers = readdirSync(base).sort((a, b) => (parseInt(a) || 0) - (parseInt(b) || 0));
      if (vers.length) return join(base, vers[vers.length - 1]!);
    }
  }
  throw new Error("Cannot locate MSVC tools root — run from an x64 VS developer shell so VCToolsInstallDir is set");
}

/// Find a file by name anywhere under `root` (the i386 ASAN libs/DLL live in
/// the MSVC tree's lib\x86 and bin\Hostx64\x86).
function findUnder(root: string, name: string): string | undefined {
  const stack = [root];
  while (stack.length) {
    const dir = stack.pop()!;
    let entries;
    try {
      entries = readdirSync(dir, { withFileTypes: true });
    } catch {
      continue;
    }
    for (const e of entries) {
      const p = join(dir, e.name);
      if (e.isDirectory()) stack.push(p);
      else if (e.name === name) return p;
    }
  }
  return undefined;
}

export function ensureI586AsanRuntime(cfg: Config, toolchain: Toolchain): void {
  const clang = toolchain.cc;
  const clangRt = findClangResourceDir(llvmRootFromClang(clang));
  const msvc = msvcRoot();

  const rd = join(cfg.buildDir, "clang-rt-i586");
  const target = join(rd, "lib", "i586-pc-windows-msvc");

  // Mirror the clang resource dir's builtin headers once. xcopy (not
  // fs.cpSync) handles Windows quirks (ACLs, long paths) robustly.
  if (!existsSync(join(rd, "include", "stddef.h"))) {
    mkdirSync(rd, { recursive: true });
    execSync(`xcopy "${join(clangRt, "include")}" "${join(rd, "include")}" /E /I /Y /H /C`, {
      stdio: "ignore",
    });
  }

  // Only the i586-pc-windows-msvc runtime lib dir is consulted by
  // `-resource-dir` for this target, so we create just it (not the whole
  // clang lib/, which is mostly x86_64 and unnecessary here).
  mkdirSync(target, { recursive: true });

  // MSVC i386 ASAN runtime (import libs + DLL), renamed to what clang-cl
  // expects in a target-specific resource-dir.
  const pick = (name: string, out: string) => {
    const src = findUnder(msvc, name);
    if (!src) throw new Error(`MSVC i386 ASAN file ${name} not found under ${msvc}`);
    copyFileSync(src, join(target, out));
  };
  pick("clang_rt.asan_dynamic-i386.lib", "clang_rt.asan_dynamic.lib");
  pick("clang_rt.asan_dynamic_runtime_thunk-i386.lib", "clang_rt.asan_dynamic_runtime_thunk.lib");
  pick("clang_rt.asan_static_runtime_thunk-i386.lib", "clang_rt.asan_static_runtime_thunk.lib");

  // Deploy the runtime DLL next to the built binary so it loads at runtime.
  const dll = findUnder(msvc, "clang_rt.asan_dynamic-i386.dll");
  if (dll) copyFileSync(dll, join(cfg.buildDir, "clang_rt.asan_dynamic-i386.dll"));
}
