/**
 * ICU for Windows.
 *
 * Two modes mirroring WebKit (`cfg.icu`):
 *   **prebuilt**: download unicode-org/icu's 78.3 Win MSVC2022 zip (DLLs +
 *     import libs + headers). The DLLs are built /MD against VCRUNTIME140 /
 *     api-ms-win-crt, so they do NOT run on Windows 9x/XP.
 *   **local**: build ICU 78.3 from a local source checkout (`vendor/icu/` or
 *     `$BUN_ICU_PATH`) as **static /MT** libs via `build-icu.ps1` (msbuild).
 *     The win9x/XP profiles use `local` so bun links ICU in with no ICU DLL
 *     dependency (the C++ side already compiles with `U_STATIC_IMPLEMENTATION`).
 *
 * Posix uses the system ICU, so this dep is Windows-only.
 *
 * Vendor patches for the ICU SOURCE live in `vendor-patches/icu/` and are
 * applied by the dep pipeline after fetch/checkout (same as WebKit's
 * `vendor-patches/WebKit/`).
 */

import { existsSync } from "node:fs";
import { resolve } from "node:path";
import type { Config } from "../config.ts";
import { type Dependency, depBuildDir } from "../source.ts";

const ICU_VERSION = "78.3";

function prebuiltUrl(cfg: Config): string {
  const platform = cfg.x64 ? "Win64" : cfg.x86 ? "Win32" : "WinARM64";
  return `https://github.com/unicode-org/icu/releases/download/release-${ICU_VERSION}/icu4c-${ICU_VERSION}-${platform}-MSVC2022.zip`;
}

function prebuiltDestDir(cfg: Config): string {
  return resolve(cfg.cacheDir, `icu-${ICU_VERSION}-${cfg.arch}`);
}

/**
 * Local ICU source root (the dir containing `allinone/allinone.sln`).
 * `$BUN_ICU_PATH` points at it directly; otherwise `vendor/icu` is the
 * unicode-org/icu clone, whose C source lives at `icu4c/source`.
 */
function icuSrcDir(cfg: Config): string {
  const env = process.env.BUN_ICU_PATH;
  if (env) return env;
  const vendorIcu = resolve(cfg.cwd, "vendor", "icu");
  const icu4cSource = resolve(vendorIcu, "icu4c", "source");
  return existsSync(icu4cSource) ? icu4cSource : vendorIcu;
}

/** Where the local build lands (libs + generated headers). Per-profile via buildDir. */
export function icuBuildDir(cfg: Config): string {
  return depBuildDir(cfg, "icu");
}

/** ICU root as consumed by WebKit's cmake (`ICU_ROOT`): prebuilt dest or local build dir. */
export function icuRootDir(cfg: Config): string {
  return cfg.icu === "prebuilt" ? prebuiltDestDir(cfg) : icuBuildDir(cfg);
}

export function localIcuLibs(cfg: Config): string[] {
  const dir = icuBuildDir(cfg);
  return [resolve(dir, "lib", "icudt.lib"), resolve(dir, "lib", "icuin.lib"), resolve(dir, "lib", "icuuc.lib")];
}

function prebuiltIcuLibs(cfg: Config): string[] {
  const d = cfg.debug ? "d" : "";
  return [`lib/icudt${d}.lib`, `lib/icuin${d}.lib`, `lib/icuuc${d}.lib`];
}

export const icu: Dependency = {
  name: "icu",
  versionMacro: "ICU",
  enabled: cfg => cfg.windows, // posix uses system ICU
  source: cfg => {
    if (cfg.icu === "prebuilt") {
      return {
        kind: "prebuilt",
        url: prebuiltUrl(cfg),
        identity: `icu-${ICU_VERSION}-${cfg.arch}-msvc2022`,
        destDir: prebuiltDestDir(cfg),
      };
    }
    const env = process.env.BUN_ICU_PATH;
    const path = icuSrcDir(cfg);
    return {
      kind: "local",
      path,
      hint: env
        ? `$BUN_ICU_PATH is set to '${env}' but that path does not contain an ICU checkout`
        : "Clone unicode-org/icu to vendor/icu/, or set $BUN_ICU_PATH to an existing checkout (mirrors $BUN_WEBKIT_PATH)",
    };
  },
  patches: cfg =>
    cfg.icu === "local"
      ? [
          "vendor-patches/icu/build-icu-source.patch",
          "vendor-patches/icu/makedata-filterfile.patch",
          "vendor-patches/icu/makedata-skip-testdata.patch",
        ]
      : [],
  build: cfg => {
    if (cfg.icu === "prebuilt") return { kind: "none" };

    // Local: ICU has no CMake build, so drive msbuild through a script build
    // (build-icu.ps1 → static /MT libs). The libs are the script's declared
    // outputs.
    const out = icuBuildDir(cfg);
    const srcDir = icuSrcDir(cfg);
    const platform = cfg.x64 ? "x64" : cfg.x86 ? "x86" : "ARM64";
    const script = resolve(cfg.cwd, "scripts", "build", "deps", "icu", "build-icu.ps1");
    // ASAN: build ICU with the Release config so it uses /MD (MultiThreadedDLL,
    // _ITERATOR_DEBUG_LEVEL=0), matching the /MD used across bun + WebKit.
    // Debug ICU would produce /MDd (level 2) and fail the link.
    const icuBuildType = cfg.asan ? "Release" : cfg.debug ? "Debug" : "Release";
    const dynCrt = cfg.asan ? "-UseDynamicCRT" : "";
    return {
      kind: "script",
      command: [
        "powershell", "-ExecutionPolicy", "Bypass", "-File", script,
        "-SourceDir", srcDir,
        "-Platform", platform,
        "-BuildType", icuBuildType,
        "-OutputDir", out,
        ...(dynCrt ? [dynCrt] : []),
      ],
      cwd: srcDir,
      outputs: localIcuLibs(cfg),
    };
  },
  provides: cfg => {
    if (cfg.icu === "prebuilt") {
      // Paths relative to prebuilt destDir — emitPrebuilt resolves them.
      return {
        libs: prebuiltIcuLibs(cfg),
        includes: ["include", "include/unicode"],
      };
    }
    return {
      libs: localIcuLibs(cfg),
      includes: [resolve(icuBuildDir(cfg), "include")],
    };
  },
};
