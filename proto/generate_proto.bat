@echo off
setlocal enabledelayedexpansion

:: Script directory
set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

:: vcpkg protoc discovery
set "VCPKG_PROTOC="
if exist "..\server\build\vcpkg_installed\x64-windows\tools\protobuf\protoc.exe" (
    set "VCPKG_PROTOC=..\server\build\vcpkg_installed\x64-windows\tools\protobuf\protoc.exe"
) else if exist "..\client\build\vcpkg_installed\x64-windows\tools\protobuf\protoc.exe" (
    set "VCPKG_PROTOC=..\client\build\vcpkg_installed\x64-windows\tools\protobuf\protoc.exe"
) else (
    where protoc >nul 2>nul
    if !errorlevel! equ 0 (
        for /f "delims=" %%i in ('where protoc') do set "VCPKG_PROTOC=%%i"
    )
)

if "%VCPKG_PROTOC%"=="" (
    echo Error: protoc not found.
    exit /b 1
)

echo Using protoc: %VCPKG_PROTOC%

:: Paths
set "PROTO_SRC=."
set "SERVER_HEADER=..\server\src\header"
set "SERVER_SOURCE=..\server\src\sources"
set "CLIENT_HEADER=..\client\src\header"
set "CLIENT_SOURCE=..\client\src\sources"

:: Ensure directories exist
if not exist "%SERVER_HEADER%" mkdir "%SERVER_HEADER%"
if not exist "%SERVER_SOURCE%" mkdir "%SERVER_SOURCE%"
if not exist "%CLIENT_HEADER%" mkdir "%CLIENT_HEADER%"
if not exist "%CLIENT_SOURCE%" mkdir "%CLIENT_SOURCE%"

:: Compile each proto file
for %%f in (*.proto) do (
    echo Processing %%f...

    :: Server
    "%VCPKG_PROTOC%" --proto_path="%PROTO_SRC%" --cpp_out="%SERVER_HEADER%" "%%f"
    
    :: Client
    "%VCPKG_PROTOC%" --proto_path="%PROTO_SRC%" --cpp_out="%CLIENT_HEADER%" "%%f"

    echo Generated headers and sources from %%f
)

:: Move .pb.cc files
move /y "%SERVER_HEADER%\*.pb.cc" "%SERVER_SOURCE%\" >nul 2>nul
move /y "%CLIENT_HEADER%\*.pb.cc" "%CLIENT_SOURCE%\" >nul 2>nul

echo All proto files generated!
pause
