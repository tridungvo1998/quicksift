@echo off
setlocal
rem Microsoft Store/MSIX build. Requires Store identity values and corresponding-source URL.
rem A failure prints "Build failed / What to do / Technical detail" (scripts\build-failure-help.ps1).
where powershell.exe >nul 2>nul
if errorlevel 1 goto no_powershell
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package-msix.ps1" %*
set "QS_EXIT=%ERRORLEVEL%"
if "%QS_EXIT%"=="0" exit /b 0
echo.
echo MSIX packaging did not finish (exit code %QS_EXIT%). The "Build failed" message above says why and what to do.
echo If there is no such message, open the newest QuickSift-BuildLog-*.txt file in this folder.
exit /b %QS_EXIT%

:no_powershell
echo.
echo Build failed: Windows PowerShell could not be found on this PC.
echo What to do: Windows 10 and 11 include it. Make sure C:\Windows\System32\WindowsPowerShell\v1.0 is on PATH,
echo             or repair Windows by running "sfc /scannow" as administrator, then run this file again.
echo Technical detail: "where powershell.exe" found nothing.
exit /b 9009
