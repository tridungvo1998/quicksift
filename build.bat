@echo off
setlocal
rem Portable release build: QuickSift.exe writes its own state under .\QuickSiftData.
call "%~dp0build-release.cmd" %*
exit /b %ERRORLEVEL%
