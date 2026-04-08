@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d "%~dp0"
echo --- COMPILING ---
cl /W4 /I..\include /Fe:test_core.exe test_core.c ..\core\config.c ..\core\validation.c ..\core\log.c
echo --- ERRORLEVEL: %errorlevel% ---
if %errorlevel% neq 0 (
    echo BUILD FAILED
    exit /b %errorlevel%
)
echo --- RUNNING TESTS ---
.\test_core.exe
echo --- TEST EXIT: %errorlevel% ---
