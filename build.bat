@echo off
set PRESET=x64-debug

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
