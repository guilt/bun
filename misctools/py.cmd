@echo off
setlocal enabledelayedexpansion
rem ===========================================================================
rem  py.cmd -- version-aware Python launcher shim for the win9x build tree.
rem
rem  A single entry point that forwards to a real interpreter on the host,
rem  supporting both Python 2 and Python 3 like the real `py` launcher:
rem
rem      py.cmd <args>                 default interpreter (py3 preferred)
rem      py.cmd -3 <args>              force Python 3 (any patch)
rem      py.cmd -3.8 <args>            force Python 3.8 exactly
rem      py.cmd -2 <args>              force Python 2 (any patch)
rem      py.cmd -2.7 <args>            force Python 2.7 exactly
rem
rem  A leading "-2"/"-3"/"-2.x"/"-3.x" token is consumed as the selector; every
rem  other argument is passed through verbatim. This matches how the build
rem  scripts and some tools invoke `py` (they like to pass "-3").
rem
rem  Resolution uses the *_HOME variables set by AUTOEXEC.CMD
rem  (see ~\CMD\AUTOEXEC.CMD); no path is ever hardcoded:
rem
rem    -3 / -3.x (in order):
rem       1. %PYTHON3_HOME%\python.exe / Scripts\python.exe
rem       2. %VIRTUAL_ENV%\Scripts\python.exe             (an activated venv)
rem       3. python%PATCH%.exe / python3.exe / python.exe (on %PATH%),
rem          falling back to the patch implied by %PYTHON3_VERSION%
rem
rem    -2 / -2.x (in order):
rem       1. %PYTHON2_HOME%\python.exe / Scripts\python.exe
rem       2. %VIRTUAL_ENV%\Scripts\python.exe
rem       3. python2%PATCH%.exe / python2.exe / python.exe (on %PATH%),
rem          falling back to the patch implied by %PYTHON2_VERSION%
rem
rem    (no selector):
rem       1. %PYTHON_HOME%\python.exe / Scripts\python.exe
rem       2. %PYTHON3_HOME%\python.exe / Scripts\python.exe
rem       3. python3.exe -> python.exe -> python2.exe (on %PATH%),
rem          falling back to the patch implied by %PYTHON_VERSION%
rem
rem  If nothing is found, prints an actionable error and exits with code 1.
rem  Otherwise the interpreter's own exit code is passed through so callers can
rem  branch on it (e.g. the ICU data build, misctools\pe_disable_aslr.py).
rem ===========================================================================

rem ---- 1. consume ONE leading -2 / -3 / -2.x / -3.x selector ------------------
set "_VERSION="
set "_PATCH="
set "_CONSUMED="
:parsever
if "%~1"=="" goto :parseverdone
set "_TOK=%~1"
set "_VERMAJ=!_TOK:~0,2!"
if "!_VERMAJ!"=="-3" goto :gotver
if "!_VERMAJ!"=="-2" goto :gotver
goto :parseverdone
:gotver
if "!_VERMAJ!"=="-3" set "_VERSION=3"
if "!_VERMAJ!"=="-2" set "_VERSION=2"
set "_PATCH=!_TOK:~2!"
if defined _PATCH if "!_PATCH:~0,1!"=="." set "_PATCH=!_PATCH:~1!"
set "_CONSUMED=1"
shift
goto :parseverdone
:parseverdone

rem ---- 2. rebuild the remaining argument list verbatim -------------------------
set "_ARGS="
:argsloop
if "%~1"=="" goto :argsdone
set "_ARGS=!_ARGS! "%~1""
shift
goto argsloop
:argsdone

rem ---- 3. locate an interpreter -------------------------------------------------
set "_PY="

if "%_VERSION%"=="3" goto :pick3
if "%_VERSION%"=="2" goto :pick2

rem ---- no selector: PYTHON_HOME, then PYTHON3_HOME, then PATH -------------------
if defined PYTHON_HOME call :probe_dir "%PYTHON_HOME%"
if not defined _PY      if defined PYTHON3_HOME call :probe_dir "%PYTHON3_HOME%"
if not defined _PY      call :probe python3.exe
if not defined _PY      call :probe python.exe
if not defined _PY      call :probe python2.exe
if not defined _PY      if defined PYTHON_VERSION call :probe_version "%PYTHON_VERSION%" 3
goto :run

rem ---- Python 3 -----------------------------------------------------------------
:pick3
if defined PYTHON3_HOME call :probe_dir "%PYTHON3_HOME%"
if not defined _PY      if defined VIRTUAL_ENV call :probe_dir "%VIRTUAL_ENV%"
if not defined _PY      if defined _PATCH call :probe python!_PATCH!.exe
if not defined _PY      if not defined _PATCH if defined PYTHON3_VERSION call :probe_version "%PYTHON3_VERSION%" 3
if not defined _PY      call :probe python3.exe
if not defined _PY      call :probe python.exe
goto :run

rem ---- Python 2 -----------------------------------------------------------------
:pick2
if defined PYTHON2_HOME call :probe_dir "%PYTHON2_HOME%"
if not defined _PY      if defined VIRTUAL_ENV call :probe_dir "%VIRTUAL_ENV%"
if not defined _PY      if defined _PATCH call :probe python!_PATCH!.exe
if not defined _PY      if not defined _PATCH if defined PYTHON2_VERSION call :probe_version "%PYTHON2_VERSION%" 2
if not defined _PY      call :probe python2.exe
if not defined _PY      call :probe python.exe
goto :run

:run
if not defined _PY goto :notfound
"%_PY%" %_ARGS%
set "_RC=%ERRORLEVEL%"
if "%_RC%"=="9009" goto :notfound
endlocal & exit /b %_RC%

:notfound
echo py.cmd: no Python interpreter found.
echo   Set PYTHON_HOME / PYTHON3_HOME / PYTHON2_HOME (AUTOEXEC.CMD convention),
echo   or PYTHON_VERSION / PYTHON3_VERSION / PYTHON2_VERSION, or add
echo   python3.exe to %%PATH%%.
endlocal & exit /b 1

rem ---- helpers ------------------------------------------------------------------
rem :probe NAME - find a command name on %%PATH%%.
rem Uses %%~$PATH:1 expansion (works on Windows 9x/2000/XP, unlike `where`,
rem which only ships from XP SP1 and can be absent from a bare %PATH%).
:probe
set "_CAND=%~1"
if defined _PY goto :probeout
for %%X in ("%_CAND%") do if not defined _PY set "_PY=%%~$PATH:X"
:probeout
goto :eof

rem :probe_version VER MAJOR - map an AUTOEXEC version tag (e.g. "313" or "38")
rem to "python3.13.exe"/"python3.8.exe" on %%PATH%%.
:probe_version
set "_VER=%~1"
set "_MAJ=%~2"
if not defined _VER goto :eof
if defined _PY goto :eof
set "_PATCH=!_VER:~1!"
if not defined _PATCH set "_PATCH=!_VER:~0,1!"
call :probe python!_MAJ!.!_PATCH!.exe
goto :eof

rem :probe_dir HOME - look for <HOME>\python.exe, preferring Scripts\python.exe.
:probe_dir
set "_DNAME=%~1"
if defined _PY goto :eof
if exist "%_DNAME%\Scripts\python.exe" set "_PY=%_DNAME%\Scripts\python.exe"
if not defined _PY if exist "%_DNAME%\python.exe" set "_PY=%_DNAME%\python.exe"
goto :eof