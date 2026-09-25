@echo off

echo ============================================
echo Building HTTP/1.1 Calculator Server and Client
echo ============================================

where cl.exe >nul 2>nul
if %errorlevel% equ 0 goto COMPILE

set "VCVARS="
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not defined VCVARS (
    echo ERROR: Visual Studio C compiler environment not found.
    exit /b 1
)

echo Initializing VC environment from: "%VCVARS%"
call "%VCVARS%"

:COMPILE
echo Compiling server.c...
cl.exe /W3 /O2 /nologo server.c /link ws2_32.lib /out:server.exe
if %errorlevel% neq 0 (
    echo Build failed for server.c!
    exit /b 1
)

echo Compiling client.c...
cl.exe /W3 /O2 /nologo client.c /link ws2_32.lib /out:client.exe
if %errorlevel% neq 0 (
    echo Build failed for client.c!
    exit /b 1
)

echo.
echo Build successful! Generated server.exe and client.exe
del /q *.obj >nul 2>nul
exit /b 0
