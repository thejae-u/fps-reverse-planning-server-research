@echo off
set PRESET=x64-debug

where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    echo cl.exe not found in PATH. Loading Visual Studio Developer Environment...
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else (
        echo WARNING: vcvars64.bat not found at the expected path. Build might fail.
    )
)

echo Building for Windows using preset %PRESET%...

:: Server 빌드
echo Building server...
cmake --preset %PRESET% -S server
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build server/build/%PRESET%
if %errorlevel% neq 0 exit /b %errorlevel%

:: Client 빌드
echo Building client...
cmake --preset %PRESET% -S client
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build client/build/%PRESET%
if %errorlevel% neq 0 exit /b %errorlevel%

echo Build finished successfully!
pause
