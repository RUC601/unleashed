@echo off
setlocal
cd /d "%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-test-release.ps1"
set "RESULT=%ERRORLEVEL%"

echo.
if "%RESULT%"=="0" (
    echo Package created at:
    echo %~dp0dist\Unleashed-test.zip
) else (
    echo Package failed with exit code %RESULT%.
)
echo.
pause
exit /b %RESULT%
