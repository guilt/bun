# build-icu.ps1 - Build ICU statically for Windows
#
# Builds ICU from source with static CRT (/MT) for use with JavaScriptCore.
#
# Usage:
#   .\build-icu.ps1 [-Platform x64|ARM64] [-BuildType Release|Debug] [-OutputDir WebKitBuild/icu]
#
# Requirements:
#   - Visual Studio 2022 with C++ workload
#   - Python 3 (accessible via 'py -3')

param(
    [ValidateSet("x64", "x86", "ARM64")]
    [string]$Platform = "x64",

    [ValidateSet("Release", "Debug")]
    [string]$BuildType = "Release",

    [string]$OutputDir = "",

    # Pre-fetched ICU source checkout (the dep pipeline has already applied
    # vendor-patches/icu). When set, the script builds it in place instead of
    # downloading/extracting its own copy.
    [string]$SourceDir = "",

    # ASAN: use the dynamic MSVC runtime (/MD) instead of the static /MT that
    # the win9x XP-compat build normally needs. clang-cl's AddressSanitizer
    # requires the dynamic CRT; keeping ICU static-CRT would mismatch at link.
    [switch]$UseDynamicCRT = $false,

    # ASAN: path to clang-cl.exe. When set, ICU's common/i18n STATIC libs (the
    # ones linked into bun) are compiled with clang-cl + $CxxFlags instead of
    # msbuild/cl.exe. Required so ICU's MSVC-STL objects carry the same
    # annotate_string=1 / stl_asan.lib directives as WebKit/boringssl/etc. and
    # the final link doesn't fail on a /failifmismatch. The makedata STAGE 1
    # (data tools) stays msbuild — those DLLs never link into bun.
    [string]$Compiler = "",

    # ASAN: the clang-cl flag string (already includes /MD, -fsanitize=address,
    # -resource-dir, -march, /GR-, /EHs-c-, ...). Passed straight through.
    [string]$CxxFlags = ""
)

$ErrorActionPreference = "Stop"

# Git's build environment sets MAKEFLAGS (e.g. "j23"), which NMAKE rejects
# with "U1065: invalid option 'j'" when msbuild runs NMAKE custom build steps.
Remove-Item Env:MAKEFLAGS -ErrorAction SilentlyContinue

# Default output directory
if (-not $OutputDir) {
    $OutputDir = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "WebKitBuild/icu"
}

$ICU_LIB_DIR = Join-Path $OutputDir "lib"
$ICU_INCLUDE_DIR = Join-Path $OutputDir "include"

$ICU_SOURCE_URL = "https://github.com/unicode-org/icu/releases/download/release-78.3/icu4c-78.3-sources.tgz"

# Verify Python 3 is available (required for ICU data build)
try {
    $pythonVersion = & py -3 --version 2>&1
    Write-Host ":: Found Python: $pythonVersion"
} catch {
    throw "Python 3 is required to build ICU data. Please install Python 3 and ensure 'py -3' is available."
}

# Set up MSVC environment if not already loaded
if ($env:VSINSTALLDIR -eq $null) {
    Write-Host "Loading Visual Studio environment, this may take a second..."
    $vsDir = Get-ChildItem -Path "C:\Program Files\Microsoft Visual Studio\2022" -Directory
    if ($vsDir -eq $null) {
        throw "Visual Studio directory not found."
    }
    Push-Location $vsDir
    try {
        $targetArch = if ($Platform -eq "ARM64") { "arm64" } else { "amd64" }
        . (Join-Path -Path $vsDir.FullName -ChildPath "Common7\Tools\Launch-VsDevShell.ps1") -Arch $targetArch -HostArch amd64
    }
    finally { Pop-Location }
}

$null = mkdir $OutputDir -ErrorAction SilentlyContinue

# --------------------------------------------------------------------------
# Trim PATH for MSBuild custom build steps (makedata's nmake).
# MSBuild prepends its own VC/SDK dirs to PATH, and cmd.exe refuses to
# resolve ANY external command once %PATH% exceeds 8191 chars. With the long
# PATH inherited from the dev shell, the CustomBuild step's PATH balloons past
# the limit and every nmake/where/findstr call fails with "not recognized"
# (MSB8066 / exit code 9009). Replace PATH with a short, self-contained list
# covering: System32 (+ PowerShell for ICU's >8190-char command fallback), the
# ICU bin dir (the data tools need icuuc*/icuin* DLLs at runtime), the MSVC
# bin dirs (nmake/cl), Git (perl), EXTDEV (py), and the Windows SDK bin.
# --------------------------------------------------------------------------
function Get-TrimmedPath {
    $systemRoot = $env:SystemRoot
    if (-not $systemRoot) { $systemRoot = "C:\Windows" }
    $paths = @(
        "$systemRoot\System32",
        "$systemRoot",
        "$systemRoot\System32\WindowsPowerShell\v1.0"
    )

    # ICU bin dir (parent of source/) — the DLL the data tools load at runtime.
    $prefixBin = Join-Path (Split-Path -Parent $ICU_SOURCE_DIR) "bin"
    if (Test-Path $prefixBin) { $paths += $prefixBin }

    # MSVC toolset bin dirs (nmake.exe, cl.exe).
    if ($env:VCToolsInstallDir) {
        $paths += (Join-Path $env:VCToolsInstallDir "bin\Hostx64\x64")
        $paths += (Join-Path $env:VCToolsInstallDir "bin\Hostx86\x86")
    } else {
        $msvcRoot = Join-Path (Join-Path (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022") "Community\VC\Tools\MSVC")
        if (Test-Path $msvcRoot) {
            $ver = Get-ChildItem $msvcRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
            if ($ver) {
                $paths += (Join-Path $ver.FullName "bin\Hostx64\x64")
                $paths += (Join-Path $ver.FullName "bin\Hostx86\x86")
            }
        }
    }

    # Git for Windows (perl.exe for some ICU build steps).
    $gitUsr = Join-Path $env:ProgramFiles "Git\usr\bin"
    if (Test-Path $gitUsr) { $paths += $gitUsr }

    # Python launcher (py.exe).
    $extdevBin = "D:\WS\EXTDEV\Bin"
    if (Test-Path $extdevBin) { $paths += $extdevBin }

    # Visual Studio common tools (vsdevcmd etc.).
    if ($env:VSINSTALLDIR) { $paths += (Join-Path $env:VSINSTALLDIR "Common7\IDE") }

    # Windows SDK bin (rc.exe etc. for tool builds).
    $kitsBin = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
    if (Test-Path $kitsBin) {
        $sdkVer = Get-ChildItem $kitsBin -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
        if ($sdkVer) {
            $paths += (Join-Path $sdkVer.FullName "x64")
            $paths += $sdkVer.FullName
        }
    }

    return ($paths -join ";")
}

$FullPath = $env:PATH

if ($SourceDir) {
    if (-not (Test-Path $SourceDir)) {
        throw "SourceDir not found: $SourceDir"
    }
    $ICU_SOURCE_DIR = $SourceDir
    Write-Host ":: Using pre-fetched ICU source at: $ICU_SOURCE_DIR"
} else {
    $ICU_TARBALL = Join-Path $OutputDir "icu4c-src.tgz"
    $ICU_SOURCE_DIR = Join-Path $OutputDir "source"

    # --- Download ICU source ---
    if (-not (Test-Path $ICU_TARBALL) -and -not (Test-Path $ICU_SOURCE_DIR)) {
        Write-Host ":: Downloading ICU"
        Invoke-WebRequest -Uri $ICU_SOURCE_URL -OutFile $ICU_TARBALL
    }

    if (-not (Test-Path $ICU_SOURCE_DIR)) {
        Write-Host ":: Extracting ICU"
        # ICU tarball extracts to icu/ directory
        $extractDir = Split-Path -Parent $OutputDir
        & "$env:SystemRoot\System32\tar.exe" -xzf $ICU_TARBALL -C $extractDir
        if ($LASTEXITCODE -ne 0) { throw "tar failed with exit code $LASTEXITCODE" }
    }
}

if ($Platform -eq "x64") {
    $ArchFlag = "/clang:-march=nehalem"
} else {
    $ArchFlag = ""
}

# ClangCL for stage 2 so -march= limits codegen (MSVC /arch:SSE2 is a no-op on x64).
# Fall back to the default MSVC toolset when the ClangCL toolset isn't installed
# (VS "C++ Clang tools for Windows" component). The -march=nehalem flag is a
# perf nicety; correctness is identical with MSVC.
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = if (Test-Path $vswhere) { & $vswhere -latest -property installationPath } else { "" }
$clangClToolset = if ($vsPath) { Join-Path $vsPath "MSBuild\Microsoft\VC\v170\Platforms\x64\PlatformToolsets\ClangCL" } else { "" }
$haveClangCL = $Platform -eq "x64" -and (Test-Path $clangClToolset)
$ToolsetArg = if ($haveClangCL) { @("/p:PlatformToolset=ClangCL") } else { @() }
if ($Platform -eq "x64" -and -not $haveClangCL) {
    Write-Host ":: WARNING: ClangCL toolset not found; building ICU with the default MSVC toolset"
    $ArchFlag = ""
}
$MsbPlatform = if ($Platform -eq "x86") { "Win32" } else { $Platform }

# --- Function to patch vcxproj files for static library build with /MT ---
function Patch-IcuVcxProj {
    param(
        [string]$FilePath
    )

    if (-not (Test-Path $FilePath)) {
        throw "File not found: $FilePath"
    }

    # Create backup if not exists
    $BackupPath = "$FilePath.bak"
    if (-not (Test-Path $BackupPath)) {
        Copy-Item $FilePath $BackupPath
        Write-Host "  Backed up: $(Split-Path -Leaf $FilePath)"
    }

    $content = Get-Content $FilePath -Raw

    # DynamicLibrary -> StaticLibrary
    $content = $content -replace '<ConfigurationType>DynamicLibrary</ConfigurationType>', '<ConfigurationType>StaticLibrary</ConfigurationType>'

    # MultiThreadedDLL -> MultiThreaded  (unless ASAN, which needs the dynamic /MD CRT)
    if (-not $UseDynamicCRT) {
        $content = $content -replace '<RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>', '<RuntimeLibrary>MultiThreaded</RuntimeLibrary>'
        $content = $content -replace '<RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary>', '<RuntimeLibrary>MultiThreadedDebug</RuntimeLibrary>'
    }

    # Add U_STATIC_IMPLEMENTATION
    if ($content -notmatch 'U_STATIC_IMPLEMENTATION') {
        $content = $content -replace '(<PreprocessorDefinitions>)', '$1U_STATIC_IMPLEMENTATION;'
    }

    if ($ArchFlag) {
        $content = $content -replace '(<ClCompile>)', "`$1`n      <AdditionalOptions>$ArchFlag %(AdditionalOptions)</AdditionalOptions>"
    }

    # Disable /GL - lld-link cannot read LTCG object files
    $content = $content -replace '<WholeProgramOptimization>true</WholeProgramOptimization>', '<WholeProgramOptimization>false</WholeProgramOptimization>'

    # Remove DLL-specific link settings
    $content = $content -replace '<OutputFile>[^<]*\.(dll|DLL)</OutputFile>', ''
    $content = $content -replace '<ImportLibrary>[^<]*</ImportLibrary>', ''

    # Strip .rc — rc.exe cannot parse clang stddef.h and static libs do not need version resources.
    $content = $content -replace "(?s)<ResourceCompile[^>]*>.*?</ResourceCompile>", ""
    $content = $content -replace "<ResourceCompile[^>]*/>", ""

    # For stubdata - remove resource-only DLL settings
    if ($FilePath -match "stubdata") {
        $content = $content -replace '<NoEntryPoint>true</NoEntryPoint>', ''
        $content = $content -replace '<TurnOffAssemblyGeneration>true</TurnOffAssemblyGeneration>', ''
    }

    Set-Content $FilePath $content -NoNewline
    Write-Host "  Patched: $(Split-Path -Leaf $FilePath)"
}

# --- Patch makedata.mak to skip DLL copy for static build ---
$makedataMak = Join-Path $ICU_SOURCE_DIR "data\makedata.mak"
if (Test-Path $makedataMak) {
    $makedataContent = Get-Content $makedataMak -Raw
    if ($makedataContent -notmatch '-copy "\$\(U_ICUDATA_NAME\)\.dll"') {
        Write-Host ":: Patching makedata.mak to skip DLL copy for static build..."
        $makedataContent = $makedataContent -replace '\tcopy "\$\(U_ICUDATA_NAME\)\.dll"', "`t-copy `"`$(U_ICUDATA_NAME).dll`""
        Set-Content $makedataMak $makedataContent -NoNewline
        Write-Host "  Patched: makedata.mak"
    }
}

# --- Find MSBuild ---
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere not found. Please install Visual Studio."
}

$vsPath = & $vswhere -latest -property installationPath
$msbuildPath = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"

if (-not (Test-Path $msbuildPath)) {
    throw "MSBuild not found at: $msbuildPath"
}

Write-Host ""
Write-Host ":: Using MSBuild: $msbuildPath"

$slnPath = Join-Path $ICU_SOURCE_DIR "allinone\allinone.sln"

# Reset the project files that STAGE 2 runtime-patches for static /MT
# (common/i18n/stubdata vcxproj + the /GL props). Without this, the NEXT run's
# tools build against the already-static-patched projects and the data tools
# (gencnval/genrb) fail to load. The vendor patches (testdata.mak, makedata.mak)
# are NOT touched — they're applied by the dep pipeline and stay in place.
$runtimePatched = @(
    "common\common.vcxproj",
    "i18n\i18n.vcxproj",
    "stubdata\stubdata.vcxproj",
    "allinone\Build.Windows.ProjectConfiguration.props"
)
$inGitWorkTree = git -C $ICU_SOURCE_DIR rev-parse --is-inside-work-tree 2>&1 | Out-String
if ($inGitWorkTree -match "true") {
    $resetArgs = @("-C", $ICU_SOURCE_DIR, "checkout", "--") + ($runtimePatched | ForEach-Object { Join-Path $ICU_SOURCE_DIR $_ })
    & git @resetArgs 2>&1 | Out-Null
    Get-ChildItem (Join-Path $ICU_SOURCE_DIR "common"), (Join-Path $ICU_SOURCE_DIR "i18n"), (Join-Path $ICU_SOURCE_DIR "stubdata") -Filter "*.vcxproj.bak" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
}

# ========================================================================
# STAGE 1: Build makedata with default DLL configuration
# ========================================================================
# The ICU tools (pkgdata, etc.) require DLL linkage to build and run.
# We build makedata first with default configuration to generate ICU data.
# The output sicudt.lib is pure data with no CRT dependencies, so it works
# with any runtime configuration.
Write-Host ""
Write-Host ":: STAGE 1: Building ICU makedata (generates ICU data)..."
Write-Host ":: ICU_PACKAGE_MODE (stage 1): -m dll (data DLL for the tools)"
$env:ICU_PACKAGE_MODE = ""

# The data tools (genrb, icupkg, ...) are built DLL-config and need the ICU
# common/i18n DLLs at runtime, so build those first and put them on PATH.
foreach ($tgt in @("common", "i18n")) {
    $dllArgs = @(
        $slnPath,
        "/t:$tgt",
        "/p:Configuration=$BuildType",
        "/p:Platform=$MsbPlatform",
        "/p:SkipUWP=true",
        "/p:WindowsTargetPlatformVersion=10.0",
        "/v:minimal"
    )
    $dllArgs += $ToolsetArg
    & $msbuildPath $dllArgs
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed for $tgt (DLL) with exit code $LASTEXITCODE" }
}
$icuBin = Join-Path $ICU_SOURCE_DIR "bin"
$env:PATH = "$icuBin;$env:PATH"

# Build a STUB icudt78.dll (from stubdata) so the data tools can load
# icuuc78d.dll, which hard-imports icudt78.dll. ICU ships stubdata.cpp
# precisely for this bootstrap ("The stub data library ... is sufficient for
# running the data building tools"). The real data DLL replaces it once the
# data build (pkgdata) runs. Note: the tools' ICU DLLs land in the ICU prefix
# bin (icu4c\bin), i.e. <source>\..\bin — NOT <source>\bin.
$icuPrefixBin = Join-Path (Split-Path -Parent $ICU_SOURCE_DIR) "bin"
$stubDll = Join-Path $icuPrefixBin "icudt78.dll"
if (-not (Test-Path $stubDll)) {
    $stubSrc = Join-Path $ICU_SOURCE_DIR "stubdata\stubdata.cpp"
    $commonInc = Join-Path $ICU_SOURCE_DIR "common"
    Write-Host ":: Building stub icudt78.dll (data tools bootstrap)..."
    & cl /nologo /LD /std:c++17 /I $commonInc $stubSrc "/DSTUBDATA_BUILD" "/link" "/OUT:$stubDll"
    if ($LASTEXITCODE -ne 0) { throw "Failed to build stub icudt78.dll with exit code $LASTEXITCODE" }
    Write-Host ":: Built stub icudt78.dll"
}

# Force a fresh data build. A stale data/out (e.g. coredata.timestamp newer
# than the sources) makes NMAKE skip the databuilder, then pkgdata fails on a
# missing icudata.lst. Remove the whole generated tree so makedata always
# regenerates it deterministically.
$icuDataOut = Join-Path $ICU_SOURCE_DIR "data\out"
if (Test-Path $icuDataOut) {
    Write-Host ":: Cleaning stale ICU data output: $icuDataOut"
    Remove-Item $icuDataOut -Recurse -Force
}

$buildArgs = @(
    $slnPath,
    "/t:makedata",
    "/p:Configuration=$BuildType",
    "/p:Platform=$MsbPlatform",
    "/p:SkipUWP=true",
    "/p:WindowsTargetPlatformVersion=10.0",
    "/v:normal"
)

# makedata's CustomBuild step shells out to NMAKE. cmd.exe stops resolving any
# external command once %PATH% exceeds 8191 chars, and MSBuild's long inherited
# PATH (plus its own prepends) blows past that → "nmake is not recognized"
# (MSB8066 / exit code 9009). Run makedata under the trimmed PATH.
$env:PATH = Get-TrimmedPath
try {
    & $msbuildPath $buildArgs
} finally {
    $env:PATH = $FullPath
}

if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed for makedata with exit code $LASTEXITCODE"
}

Write-Host ":: Built makedata successfully"

# ========================================================================
# STAGE 1a: Rebuild the data as a STATIC library (sicudt.lib)
# ========================================================================
# Stage 1 built the data DLL (icudt78.dll) so the tools can run. For the final
# link bun wants the data as a static library, so re-run makedata with
# ICU_PACKAGE_MODE=-m static. The databuilder (COREDATA_TS) is already
# up-to-date, so this only re-runs pkgdata → sicudt.lib.
$env:ICU_PACKAGE_MODE = "-m static"
Write-Host ":: STAGE 1a: Rebuilding ICU data as static library (sicudt.lib)..."
$env:PATH = Get-TrimmedPath
try {
    & $msbuildPath $buildArgs
} finally {
    $env:PATH = $FullPath
}
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed for makedata (static) with exit code $LASTEXITCODE"
}
Write-Host ":: Built static ICU data library"

# ========================================================================
# STAGE 1b: Filter ICU data
# ========================================================================
# Drop converters/translit/stringprep/confusables/unames. Bun has zero
# ucnv_/utrans_/usprep_/uspoof_ consumers (TextCodecICU is removed in
# src/bun.js/bindings/TextEncodingRegistry.cpp). Cuts sicudt.lib by ~6.8 MB.
# Most of rbnf/ goes too, but root/ja/zh/zh_Hant stay: ICU reaches those on its own
# through the algorithmic numbering systems in numberingSystems.res (see the note and
# the staleness guard in ./Dockerfile).
$binDirName = if ($Platform -eq "x64") { "bin64" } else { "bin$Platform" }
$icupkg = Join-Path $ICU_SOURCE_DIR "..\$binDirName\icupkg.exe"
$datFile = Get-ChildItem -Path (Join-Path $ICU_SOURCE_DIR "data\in") -Filter "icudt*l.dat" | Select-Object -First 1
if ((Test-Path $icupkg) -and $datFile) {
    Write-Host ":: STAGE 1b: Filtering ICU data ($($datFile.Name)) with $icupkg"
    $rmList = Join-Path $datFile.DirectoryName "rm.lst"
    & $icupkg -l $datFile.FullName |
        Where-Object { $_ -match '\.(cnv|spp|cfu)$' -or $_ -match '^cnvalias\.icu$' -or $_ -match '^translit/' -or $_ -match '^rbnf/' -or $_ -match '^unames\.icu$' } |
        Where-Object { $_ -notmatch '^rbnf/(root|res_index|ja|zh|zh_Hant)\.res$' } |
        Set-Content $rmList -Encoding ascii
    $filtered = Join-Path $datFile.DirectoryName "icudt_filtered.dat"
    & $icupkg --auto_toc_prefix -r $rmList $datFile.FullName $filtered
    if ($LASTEXITCODE -ne 0) { throw "icupkg -r failed with exit code $LASTEXITCODE" }
    Move-Item -Force $filtered $datFile.FullName
    # Force makedata to repackage from the filtered .dat.
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $ICU_SOURCE_DIR "data\out")
    $env:PATH = Get-TrimmedPath
    try {
        & $msbuildPath $buildArgs
    } finally {
        $env:PATH = $FullPath
    }
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed for makedata (filtered) with exit code $LASTEXITCODE" }
    Write-Host ":: Rebuilt makedata with filtered ICU data"
} else {
    Write-Host ":: WARNING: icupkg.exe or icudt*l.dat not found; skipping ICU data filter"
}

# ========================================================================
# STAGE 2: Rebuild common and i18n as static libraries
# ========================================================================
# ASAN path: compile ICU's common/i18n .cpp with clang-cl so the MSVC-STL
# objects carry the same ASAN STL directives (annotate_string=1, stl_asan.lib)
# as every other clang-cl dep. Non-ASAN keeps the msbuild /MT path below.
if ($Compiler) {
    # clang-cl writes warnings to stderr; with $ErrorActionPreference=Stop (set
    # at the top of the script) a plain warning becomes a terminating
    # NativeCommandError. Drop to Continue here so warnings don't kill the build;
    # real failures are still caught by the explicit exit-code check + throw.
    $ErrorActionPreference = "Continue"
    Write-Host ""
    Write-Host ":: STAGE 2: Building ICU common/i18n as static libs with clang-cl (ASAN)..."
    Write-Host ":: Compiler: $Compiler"
    Write-Host ":: CxxFlags: $CxxFlags"

    $llvmLib = Join-Path (Split-Path -Parent $Compiler) "llvm-lib.exe"
    if (-not (Test-Path $llvmLib)) { throw "llvm-lib.exe not found next to clang-cl at: $llvmLib" }

    # Split the space-joined flag string into separate args (our flags have no
    # embedded spaces).
    $flagArray = @($CxxFlags.Split(" ") | Where-Object { $_ -ne "" })

    $null = mkdir -Force "$ICU_LIB_DIR"

    function Invoke-IcuLibClangCl {
        param(
            [string]$LibFile,
            [string]$SourceDir,
            [string[]]$Defines,
            [string[]]$Includes,
            [string]$ObjRoot
        )
        $sources = Get-ChildItem -Path $SourceDir -Filter "*.cpp" | Sort-Object Name
        New-Item -ItemType Directory -Path $ObjRoot -Force | Out-Null
        $objs = @()
        foreach ($src in $sources) {
            $obj = Join-Path $ObjRoot "$([IO.Path]::GetFileNameWithoutExtension($src.Name)).obj"
            $ccArgs = @(
                "/nologo",
                $flagArray,
                "/std:c++17",
                $Defines,
                $Includes,
                "/c", $src.FullName, "/Fo:$obj"
            )
            $ccOut = & $Compiler @ccArgs 2>&1
            $ccExit = $LASTEXITCODE
            if ($ccExit -ne 0) {
                Write-Host "clang-cl failed for $($src.FullName) (exit $ccExit):"
                $ccOut | ForEach-Object { Write-Host "  $_" }
                throw "clang-cl failed for $($src.FullName) (exit $ccExit)"
            }
            $objs += $obj
        }
        Write-Host "  Archiving $($objs.Count) objects -> $LibFile"
        & $llvmLib "/out:$LibFile" $objs
        if ($LASTEXITCODE -ne 0) { throw "llvm-lib failed for $LibFile (exit $LASTEXITCODE)" }
    }

    $definesCommon = @(
        "-DU_STATIC_IMPLEMENTATION", "-DU_ATTRIBUTE_DEPRECATED=",
        "-DU_COMMON_IMPLEMENTATION", "-DU_PLATFORM_USES_ONLY_WIN32_API=1",
        "-DNDEBUG", "-DWIN32"
    )
    $definesI18n = @(
        "-DU_STATIC_IMPLEMENTATION", "-DU_ATTRIBUTE_DEPRECATED=",
        "-DU_I18N_IMPLEMENTATION", "-DU_PLATFORM_USES_ONLY_WIN32_API=1",
        "-DNDEBUG", "-DWIN32"
    )

    $commonSrc = Join-Path $ICU_SOURCE_DIR "common"
    $i18nSrc = Join-Path $ICU_SOURCE_DIR "i18n"

    $null = mkdir -Force "$OutputDir\clang-obj"
    Invoke-IcuLibClangCl `
        -LibFile (Join-Path $ICU_LIB_DIR "icuuc.lib") `
        -SourceDir $commonSrc `
        -Defines $definesCommon `
        -Includes @("-I$commonSrc") `
        -ObjRoot "$OutputDir\clang-obj\common"

    Invoke-IcuLibClangCl `
        -LibFile (Join-Path $ICU_LIB_DIR "icuin.lib") `
        -SourceDir $i18nSrc `
        -Defines $definesI18n `
        -Includes @("-I$i18nSrc", "-I$commonSrc") `
        -ObjRoot "$OutputDir\clang-obj\i18n"

    Write-Host ":: Built ICU common/i18n static libs (clang-cl, ASAN)"
    $ErrorActionPreference = "Stop"
    # Data lib (sicudt.lib) copy + debug-named lib copies handled below.
} else {
Write-Host ""
Write-Host ":: STAGE 2: Rebuilding ICU common and i18n as static libraries with /MT..."

# Patch vcxproj files
Write-Host ":: Patching ICU vcxproj files for static build..."
foreach ($file in @("common\common.vcxproj", "i18n\i18n.vcxproj", "stubdata\stubdata.vcxproj")) {
    Patch-IcuVcxProj -FilePath (Join-Path $ICU_SOURCE_DIR $file)
}

# Patch .props file to disable /GL
$propsFile = Join-Path $ICU_SOURCE_DIR "allinone\Build.Windows.ProjectConfiguration.props"
if (Test-Path $propsFile) {
    Write-Host ":: Patching Build.Windows.ProjectConfiguration.props to disable /GL..."
    $propsContent = Get-Content $propsFile -Raw
    $propsContent = $propsContent -replace '<WholeProgramOptimization>true</WholeProgramOptimization>', '<WholeProgramOptimization>false</WholeProgramOptimization>'
    $propsContent = $propsContent -replace '<LinkTimeCodeGeneration>UseLinkTimeCodeGeneration</LinkTimeCodeGeneration>', '<LinkTimeCodeGeneration></LinkTimeCodeGeneration>'
    Set-Content $propsFile $propsContent -NoNewline
    Write-Host "  Patched: Build.Windows.ProjectConfiguration.props"
}

# Rebuild common and i18n with /MT
foreach ($target in @("common", "i18n")) {
    Write-Host ":: Building ICU $target with /MT..."

    $buildArgs = @(
        $slnPath,
        "/t:$target",
        "/p:Configuration=$BuildType",
        "/p:Platform=$MsbPlatform",
        "/p:SkipUWP=true",
        "/p:WindowsTargetPlatformVersion=10.0",
        "/m",
        "/v:minimal"
    )

    $buildArgs += $ToolsetArg
    & $msbuildPath $buildArgs

    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed for $target with exit code $LASTEXITCODE"
    }

    Write-Host ":: Built $target successfully"
}
}  # end ASAN-else: non-clang-cl (msbuild) STAGE 2 for common/i18n

# --- Copy output files to expected locations ---
Write-Host ""
Write-Host ":: Copying ICU output files..."

# Copy-Item can fail transiently with "user-mapped section open" when another
# process (AV scan, search indexer, a leftover ICU tool) has the source or
# destination file memory-mapped. Retry a few times before giving up.
function Copy-IcuFile {
    param(
        [string]$Source,
        [string]$Destination,
        [switch]$Recursive,
        [int]$MaxRetries = 5,
        [int]$RetryDelayMs = 2000
    )

    for ($attempt = 1; $attempt -le $MaxRetries; $attempt++) {
        try {
            if ($Recursive) {
                Copy-Item -Recurse -Force $Source $Destination -ErrorAction Stop
            } else {
                Copy-Item -Force $Source $Destination -ErrorAction Stop
            }
            return
        } catch {
            if ($attempt -lt $MaxRetries) {
                Write-Host "  Copy-Item failed (attempt $attempt/$MaxRetries): $($_.Exception.Message). Retrying in $RetryDelayMs ms..."
                Start-Sleep -Milliseconds $RetryDelayMs
            } else {
                throw
            }
        }
    }
}

$null = mkdir -Force "$ICU_INCLUDE_DIR/unicode"
$null = mkdir -Force $ICU_LIB_DIR

# Copy headers
Copy-IcuFile -Source "$ICU_SOURCE_DIR/common/unicode/*" -Destination "$ICU_INCLUDE_DIR/unicode" -Recursive
Copy-IcuFile -Source "$ICU_SOURCE_DIR/i18n/unicode/*" -Destination "$ICU_INCLUDE_DIR/unicode" -Recursive

# common/i18n static libs. clang-cl (ASAN) already wrote icuuc.lib/icuin.lib
# directly in STAGE 2; the msbuild path outputs common.lib/i18n.lib which we
# copy here.
if (-not $Compiler) {
    # MSBuild outputs to: <project>/<Platform>/<Configuration>/<project>.lib
    $commonLibSrc = Join-Path $ICU_SOURCE_DIR "common\$Platform\$BuildType\common.lib"
    $i18nLibSrc = Join-Path $ICU_SOURCE_DIR "i18n\$Platform\$BuildType\i18n.lib"

    if (Test-Path $commonLibSrc) {
        Copy-IcuFile -Source $commonLibSrc -Destination "$ICU_LIB_DIR/icuuc.lib"
        Write-Host "  Copied: common.lib -> icuuc.lib"
    } else {
        throw "ICU common library not found at: $commonLibSrc"
    }

    if (Test-Path $i18nLibSrc) {
        Copy-IcuFile -Source $i18nLibSrc -Destination "$ICU_LIB_DIR/icuin.lib"
        Write-Host "  Copied: i18n.lib -> icuin.lib"
    } else {
        throw "ICU i18n library not found at: $i18nLibSrc"
    }
}

# ICU data library - output location depends on platform
$binDir = if ($Platform -eq "x64") { "bin64" } else { "bin$Platform" }
$icuDataLibSrc = Join-Path $ICU_SOURCE_DIR "..\$binDir\sicudt73.lib"

# Check alternative locations
if (-not (Test-Path $icuDataLibSrc)) {
    $icuDataLibSrc = Join-Path $ICU_SOURCE_DIR "data\out\tmp\sicudt73.lib"
}
if (-not (Test-Path $icuDataLibSrc)) {
    $icuDataLibSrc = Join-Path $ICU_SOURCE_DIR "data\out\sicudt73.lib"
}
if (-not (Test-Path $icuDataLibSrc)) {
    # STAGE 1's makedata puts the static data archive inside the source's
    # data/out (e.g. data\out\build\icudt78l\sicudt78.lib).
    $foundLib = Get-ChildItem -Path (Join-Path $ICU_SOURCE_DIR "data\out") -Recurse -Filter "sicudt*.lib" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($foundLib) {
        $icuDataLibSrc = $foundLib.FullName
        Write-Host ":: Found ICU data library at: $icuDataLibSrc"
    }
}
if (-not (Test-Path $icuDataLibSrc)) {
    $foundLib = Get-ChildItem -Path $OutputDir -Recurse -Filter "sicudt*.lib" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($foundLib) {
        $icuDataLibSrc = $foundLib.FullName
        Write-Host ":: Found ICU data library at: $icuDataLibSrc"
    }
}

if (Test-Path $icuDataLibSrc) {
    Copy-IcuFile -Source $icuDataLibSrc -Destination "$ICU_LIB_DIR/icudt.lib"
    Write-Host "  Copied: $(Split-Path -Leaf $icuDataLibSrc) -> icudt.lib"
} else {
    Write-Host ":: WARNING: ICU data library not found. Listing generated files..."
    Get-ChildItem -Path $OutputDir -Recurse -Filter "*.lib" | ForEach-Object {
        Write-Host "    Found: $($_.FullName)"
    }
    throw "ICU data library not found. Expected at: $icuDataLibSrc"
}

if ($UseDynamicCRT) {
    # ASAN (/MD, Release config): cmake's FindICU searches for the debug-named
    # import libs (icudtd/icuind/icuucd) to populate the Debug config of the
    # ICU::* imported targets. They are the same /MD libs — provide the names.
    foreach ($pair in @(@("icudt.lib", "icudtd.lib"), @("icuin.lib", "icuind.lib"), @("icuuc.lib", "icuucd.lib"))) {
        $src = Join-Path $ICU_LIB_DIR $pair[0]
        if (Test-Path $src) {
            Copy-IcuFile -Source $src -Destination (Join-Path $ICU_LIB_DIR $pair[1])
            Write-Host "  Copied: $($pair[0]) -> $($pair[1])"
        }
    }
}

if ($Platform -eq "x86") {
    # For x86, the script requires modifications to support 32-bit builds.
    # Build the x86 version using the same MSBuild process as x64.
    # (The original script's MSBuild steps below handle x86 via Win32 platform mapping.)
    Write-Host ":: Building for x86 (Win32 platform)..."
}

Write-Host ":: ICU build completed successfully!"