@echo off
setlocal enabledelayedexpansion

:: ── Setup MSVC environment ─────────────────────────────────────────
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

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

:: ── Include paths ──────────────────────────────────────────────────
set INCLUDES=/I"%ROOT%\include" /I"%CIMGUI%" /I"%IMGUI%" /I"%BACKENDS%" /I"%SRC_APP%"

:: ── Compiler flags ─────────────────────────────────────────────────
set CFLAGS=/nologo /W4 /O2 /DNDEBUG /DUNICODE /D_UNICODE
set CPPFLAGS=%CFLAGS% /EHsc /std:c++17

:: ── C++ sources (imgui + cimgui + backends + renderer) ─────────────
set CPP_SRCS=
set CPP_SRCS=%CPP_SRCS% "%IMGUI%\imgui.cpp"
set CPP_SRCS=%CPP_SRCS% "%IMGUI%\imgui_draw.cpp"
set CPP_SRCS=%CPP_SRCS% "%IMGUI%\imgui_tables.cpp"
set CPP_SRCS=%CPP_SRCS% "%IMGUI%\imgui_widgets.cpp"
set CPP_SRCS=%CPP_SRCS% "%IMGUI%\imgui_demo.cpp"
set CPP_SRCS=%CPP_SRCS% "%CIMGUI%\cimgui.cpp"
set CPP_SRCS=%CPP_SRCS% "%BACKENDS%\imgui_impl_win32.cpp"
set CPP_SRCS=%CPP_SRCS% "%BACKENDS%\imgui_impl_dx11.cpp"
set CPP_SRCS=%CPP_SRCS% "%SRC_APP%\renderer.cpp"
set CPP_SRCS=%CPP_SRCS% "%SRC_SVC%\audio_status.cpp"

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

:: ── Link ───────────────────────────────────────────────────────────
::
:: Two binaries from the same .obj set:
::   OpenA2DP.exe       - GUI binary, /SUBSYSTEM:WINDOWS (no console flash)
::   OpenA2DP-cli.exe   - CLI binary, /SUBSYSTEM:CONSOLE (cmd waits for it)
::
:: main.c defines BOTH wWinMain and wmain; the linker pulls in the
:: appropriate one for each subsystem and the other becomes dead code.
echo --- Linking GUI binary ---
set LIBS=d3d11.lib dxgi.lib user32.lib gdi32.lib shell32.lib dwmapi.lib bthprops.lib ole32.lib propsys.lib advapi32.lib setupapi.lib
link /nologo /subsystem:windows /out:"%EXE_GUI%" %OUTDIR%\*.obj %LIBS%
if %errorlevel% neq 0 (
    echo GUI link FAILED
    exit /b 1
)

echo --- Linking CLI binary ---
link /nologo /subsystem:console /out:"%EXE_CLI%" %OUTDIR%\*.obj %LIBS%
if %errorlevel% neq 0 (
    echo CLI link FAILED
    exit /b 1
)

echo --- Build OK: %EXE_GUI% + %EXE_CLI% ---
