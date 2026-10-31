@echo off
setlocal
rem Portable release build: QuickSift.exe writes its own state under .\QuickSiftData.
rem build-release.cmd explains any failure in plain words and keeps its exit code.
call "%~dp0build-release.cmd" %*
exit /b %ERRORLEVEL%
