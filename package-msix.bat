@echo off
setlocal
rem Microsoft Store/MSIX build. Requires Store identity values and corresponding-source URL.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package-msix.ps1" %*
exit /b %ERRORLEVEL%
