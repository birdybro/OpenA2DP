@echo off
setlocal enabledelayedexpansion

:: ── Setup MSVC environment ─────────────────────────────────────────
::
:: If cl.exe is already on PATH (e.g. CI runners that use
:: ilammy/msvc-dev-cmd to set up the environment, or a Developer
:: Command Prompt the user opened manually) skip the local
:: vcvarsall.bat call.  Otherwise fall back to the hardcoded
:: VS 2026 path used for local development on Kevin's machine.
where cl.exe >nul 2>&1
if errorlevel 1 (
    call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
)
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: cl.exe not found.  Either run from a Developer Command
    echo Prompt, or install Visual Studio with "Desktop development with C++".
    exit /b 1
)

:: ── Paths ──────────────────────────────────────────────────────────
set ROOT=%~dp0..
set SRC_APP=%ROOT%\app
set SRC_CORE=%ROOT%\core
set SRC_SVC=%ROOT%\service
set CIMGUI=%ROOT%\third_party\cimgui
set IMGUI=%CIMGUI%\imgui
set BACKENDS=%IMGUI%\backends
set OUTDIR=%ROOT%\build
set EXE_GUI=%OUTDIR%\OpenA2DP.exe
set EXE_CLI=%OUTDIR%\OpenA2DP-cli.exe

if not exist "%OUTDIR%" mkdir "%OUTDIR%"

:: ── Clean stale .obj / .exp / .lib files ───────────────────────────
:: The link step uses %OUTDIR%\*.obj as a glob, so any leftover .obj
:: from a renamed or deleted source file would silently get pulled in.
:: Wipe them every build — the project's small enough that a full
:: recompile is fast and avoids stale-symbol bugs entirely.
del /q "%OUTDIR%\*.obj" 2>nul
del /q "%OUTDIR%\*.res" 2>nul
del /q "%OUTDIR%\*.exp" 2>nul
del /q "%OUTDIR%\*.lib" 2>nul

:: ── Include paths ──────────────────────────────────────────────────
set INCLUDES=/I"%ROOT%\include" /I"%CIMGUI%" /I"%IMGUI%" /I"%BACKENDS%" /I"%SRC_APP%"

:: ── Compiler flags ─────────────────────────────────────────────────
::
:: Optimization stack:
::   /O2     - speed-favored optimizations (default for an interactive app)
::   /GL     - whole-program optimization (cross-TU inlining + dead-code)
::   /Gy     - function-level linking, required for /OPT:REF to drop funcs
::   /MT     - static CRT, no VC++ redistributable needed on target machine
::   /Zc:inline - strip unreferenced inline functions from .obj
::
:: C++ adds /GR- (no RTTI, we don't use dynamic_cast/typeid).
set CFLAGS=/nologo /W4 /O2 /GL /Gy /MT /Zc:inline /DNDEBUG /DUNICODE /D_UNICODE
set CPPFLAGS=%CFLAGS% /EHsc /GR- /std:c++17

:: ── C++ sources (imgui + cimgui + backends + renderer) ─────────────
set CPP_SRCS=
set CPP_SRCS=%CPP_SRCS% "%IMGUI%\imgui.cpp"
set CPP_SRCS=%CPP_SRCS% "%IMGUI%\imgui_draw.cpp"
set CPP_SRCS=%CPP_SRCS% "%IMGUI%\imgui_tables.cpp"
set CPP_SRCS=%CPP_SRCS% "%IMGUI%\imgui_widgets.cpp"
:: imgui_demo.cpp is required because cimgui.cpp exports wrappers
:: for ShowDemoWindow / ShowAboutWindow / etc. that reference its
:: symbols.  /Gy + /OPT:REF will still drop the unused functions
:: at link time.
set CPP_SRCS=%CPP_SRCS% "%IMGUI%\imgui_demo.cpp"
set CPP_SRCS=%CPP_SRCS% "%CIMGUI%\cimgui.cpp"
set CPP_SRCS=%CPP_SRCS% "%BACKENDS%\imgui_impl_win32.cpp"
set CPP_SRCS=%CPP_SRCS% "%BACKENDS%\imgui_impl_dx11.cpp"
set CPP_SRCS=%CPP_SRCS% "%SRC_APP%\renderer.cpp"
set CPP_SRCS=%CPP_SRCS% "%SRC_SVC%\audio_status.cpp"
set CPP_SRCS=%CPP_SRCS% "%SRC_SVC%\remote_events.cpp"
set CPP_SRCS=%CPP_SRCS% "%SRC_SVC%\smtc_observer.cpp"

:: ── C sources (core + main) ───────────────────────────────────────
set C_SRCS=
set C_SRCS=%C_SRCS% "%SRC_CORE%\config.c"
set C_SRCS=%C_SRCS% "%SRC_CORE%\validation.c"
set C_SRCS=%C_SRCS% "%SRC_CORE%\log.c"
set C_SRCS=%C_SRCS% "%SRC_CORE%\stats.c"
set C_SRCS=%C_SRCS% "%SRC_CORE%\history.c"
set C_SRCS=%C_SRCS% "%SRC_SVC%\device_enum.c"
set C_SRCS=%C_SRCS% "%SRC_SVC%\actions.c"
set C_SRCS=%C_SRCS% "%SRC_SVC%\auto_heal.c"
set C_SRCS=%C_SRCS% "%SRC_SVC%\hfp_watchdog.c"
set C_SRCS=%C_SRCS% "%SRC_SVC%\driver_control.c"
set C_SRCS=%C_SRCS% "%SRC_SVC%\device_probe.c"
set C_SRCS=%C_SRCS% "%SRC_SVC%\registry_probe.c"
set C_SRCS=%C_SRCS% "%SRC_SVC%\altdriver_config.c"
set C_SRCS=%C_SRCS% "%SRC_APP%\panels.c"
set C_SRCS=%C_SRCS% "%SRC_APP%\cli.c"
set C_SRCS=%C_SRCS% "%SRC_APP%\tray.c"
set C_SRCS=%C_SRCS% "%SRC_APP%\main.c"

:: ── Compile C++ ────────────────────────────────────────────────────
echo --- Compiling C++ ---
cl %CPPFLAGS% %INCLUDES% /c %CPP_SRCS% /Fo"%OUTDIR%\\"
if %errorlevel% neq 0 (
    echo C++ compilation FAILED
    exit /b 1
)

:: ── Compile C ──────────────────────────────────────────────────────
echo --- Compiling C ---
cl %CFLAGS% %INCLUDES% /c %C_SRCS% /Fo"%OUTDIR%\\"
if %errorlevel% neq 0 (
    echo C compilation FAILED
    exit /b 1
)

:: ── Compile VERSIONINFO + ICON resources ──────────────────────────
:: Per-binary so each .exe has its own OriginalFilename / etc.
:: /I "%ROOT%\include" lets the .rc files include oa2dp_resource.h.
:: /I "%ROOT%" lets them reference icon.ico via a relative path.
echo --- Compiling resources ---
rc /nologo /I "%ROOT%\include" /I "%ROOT%" /fo "%OUTDIR%\version_gui.res" "%~dp0version_gui.rc" >nul
if %errorlevel% neq 0 (
    echo GUI resource compile FAILED
    exit /b 1
)
rc /nologo /I "%ROOT%\include" /I "%ROOT%" /fo "%OUTDIR%\version_cli.res" "%~dp0version_cli.rc" >nul
if %errorlevel% neq 0 (
    echo CLI resource compile FAILED
    exit /b 1
)

:: ── Link ───────────────────────────────────────────────────────────
::
:: Two binaries from the same .obj set:
::   OpenA2DP.exe       - GUI binary, /SUBSYSTEM:WINDOWS (no console flash)
::   OpenA2DP-cli.exe   - CLI binary, /SUBSYSTEM:CONSOLE (cmd waits for it)
::
:: main.c defines BOTH wWinMain and wmain; the linker pulls in the
:: appropriate one for each subsystem and the other becomes dead code.
echo --- Linking GUI binary ---
set LIBS=d3d11.lib dxgi.lib user32.lib gdi32.lib shell32.lib dwmapi.lib bthprops.lib ole32.lib propsys.lib advapi32.lib setupapi.lib runtimeobject.lib oleaut32.lib
link /nologo /subsystem:windows /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /out:"%EXE_GUI%" %OUTDIR%\*.obj "%OUTDIR%\version_gui.res" %LIBS%
if %errorlevel% neq 0 (
    echo GUI link FAILED
    exit /b 1
)

echo --- Linking CLI binary ---
link /nologo /subsystem:console /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /out:"%EXE_CLI%" %OUTDIR%\*.obj "%OUTDIR%\version_cli.res" %LIBS%
if %errorlevel% neq 0 (
    echo CLI link FAILED
    exit /b 1
)

:: Wipe link by-products that aren't needed at runtime.
del /q "%OUTDIR%\*.ilk" 2>nul
del /q "%OUTDIR%\*.exp" 2>nul
del /q "%OUTDIR%\*.lib" 2>nul

echo --- Build OK: %EXE_GUI% + %EXE_CLI% ---
