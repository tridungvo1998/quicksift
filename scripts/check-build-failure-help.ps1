# CODE GUIDE: See CODE_GUIDE.md -> "Building, testing, and opening Visual Studio".
# OWNER: Behaviour check for scripts\build-failure-help.ps1. Replays short excerpts of
# real build output through Add-QuickSiftFailureHint / Get-QuickSiftFailureHelp and
# checks which explanation wins. Runs on any PowerShell 7 (CI portable-checks job,
# pwsh on Linux) and on Windows PowerShell 5.1; it builds nothing. Keep it ASCII.
#   pwsh -NoProfile -File scripts/check-build-failure-help.ps1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$HelperPath = Join-Path $PSScriptRoot 'build-failure-help.ps1'
$Failures = New-Object 'System.Collections.Generic.List[string]'

function Test-FailureCase([string]$Name, [string[]]$Output, [string]$Message, [string]$ExpectCause, [string[]]$ExpectStepText = @(), [string]$RejectCause = '') {
    . $HelperPath    # fresh hint and vcpkg state for every case
    foreach ($Line in $Output) { Add-QuickSiftFailureHint $Line }
    $Help = Get-QuickSiftFailureHelp $Message 'Build'
    $Steps = ($Help.Steps -join "`n")
    if (@($Help.Steps).Count -lt 1 -or @($Help.Steps).Count -gt 3) { $script:Failures.Add("${Name}: expected 1-3 steps, got $(@($Help.Steps).Count)") }
    if ($Help.Cause -like 'vcpkg could not build*' -and $Steps.Contains('cmake-get-vars')) { $script:Failures.Add("${Name}: names a configuration log instead of the failing step log") }
    if ($Help.Cause -notlike $ExpectCause) { $script:Failures.Add("${Name}: cause was '$($Help.Cause)', expected like '$ExpectCause'") }
    if ($RejectCause -and $Help.Cause -like $RejectCause) { $script:Failures.Add("${Name}: cause must not be '$($Help.Cause)'") }
    foreach ($Text in $ExpectStepText) {
        if (-not $Steps.Contains($Text)) { $script:Failures.Add("${Name}: steps do not mention '$Text'") }
    }
}

$CmakeFailed = "Command failed with exit code 1: C:\Program Files\CMake\bin\cmake.exe`nCommand: cmake.exe -S . -B build`nFull build transcript: C:\src\QuickSift-BuildLog.txt"
$SdkMissing = 'The Windows SDK (part of the Visual Studio C++ tools) is missing.'

# CI run 2 (VS 2026 18.12, MSVC 14.52): dav1d failed with C7780 while cppwinrt had
# printed an informational "Windows SDK" line. It was misreported as a missing SDK.
$Run2 = @(
    '-- Generating headers for Windows SDK 10.0.26100.0',
    'CMake Error at scripts/cmake/vcpkg_execute_required_process.cmake:127 (message):',
    '    See logs for more information:',
    '-- Loading CMake variables from D:/a/_temp/vcpkg/buildtrees/dav1d/cmake-get-vars_C_CXX-x64-windows-static.cmake.log',
    '      D:\a\_temp\vcpkg\buildtrees\dav1d\package-x64-windows-static-dbg-out.log',
    'error: building dav1d:x64-windows-static failed with: BUILD_FAILED',
    '../src/1.5.3-f15718dea2.clean/include/compat/msvc/stdatomic.h(35): error C7780: function-like macro ''__has_include_next'' is not defined',
    '-- Running vcpkg install - failed',
    '  vcpkg install failed.  See logs for more information:',
    '  C:\Users\runneradmin\AppData\Local\QuickSiftBuild\cmake-vs18\vcpkg-manifest-install.log'
)
Test-FailureCase 'vcpkg port BUILD_FAILED names the port and log' $Run2 $CmakeFailed `
    "vcpkg could not build the third-party library 'dav1d' (x64-windows-static, BUILD_FAILED)." `
    @('D:\a\_temp\vcpkg\buildtrees\dav1d\package-x64-windows-static-dbg-out.log', 'issues?q=dav1d.') $SdkMissing

# Port names with features and logs of other ports: only the failing port's log is named.
Test-FailureCase 'feature spec and foreign logs' @(
    '      D:\v\buildtrees\libyuv\config-x64-windows-static-out.log',
    'error: building libavif[core,dav1d]:x64-windows-static failed with: BUILD_FAILED',
    '      D:\v\buildtrees\libavif\install-x64-windows-static-rel-out.log') $CmakeFailed `
    "vcpkg could not build the third-party library 'libavif' (x64-windows-static, BUILD_FAILED)." `
    @('D:\v\buildtrees\libavif\install-x64-windows-static-rel-out.log')
Test-FailureCase 'no port log seen' @('error: building zlib:x64-windows failed with: BUILD_FAILED') $CmakeFailed `
    "vcpkg could not build the third-party library 'zlib' (x64-windows, BUILD_FAILED)." @('buildtrees\zlib\')

# Real SDK-not-found signatures still explain a missing SDK.
Test-FailureCase 'MSB8036' @('error MSB8036: The Windows SDK version 10.0.22621.0 was not found. Install the required version of Windows SDK.') $CmakeFailed $SdkMissing
Test-FailureCase 'windows.h missing' @("fatal error C1083: Cannot open include file: 'windows.h': No such file or directory") $CmakeFailed $SdkMissing
Test-FailureCase 'no compiler' @('-- No CMAKE_CXX_COMPILER could be found.') $CmakeFailed $SdkMissing
Test-FailureCase 'no suitable SDK' @('CMake Error: could not find any Windows SDK') $CmakeFailed $SdkMissing
Test-FailureCase 'SDK missing beats port failure' @(
    'error: building zlib:x64-windows failed with: BUILD_FAILED',
    "fatal error C1083: Cannot open include file: 'kernel32.lib'") $CmakeFailed $SdkMissing

# Mentions of the SDK alone are not a missing SDK.
Test-FailureCase 'informational SDK line' @('-- Generating headers for Windows SDK 10.0.26100.0', 'CMake Error: something else') $CmakeFailed `
    'CMake could not prepare the Visual Studio project.' @() $SdkMissing
Test-FailureCase 'SDK include path in a compiler error' @(
    'C:\Program Files (x86)\Windows Kits\10\include\10.0.26100.0\um\winnt.h(1): error C2059: syntax error') "$CmakeFailed`nCommand: cmake --build build" `
    'The C++ compiler stopped with an error while building QuickSift.' @() $SdkMissing

# Environment causes stay ahead of a port failure.
Test-FailureCase 'long path wins over port failure' @(
    'error: building qtbase:x64-windows failed with: BUILD_FAILED',
    'error MSB3491: Could not write lines to file: path too long') $CmakeFailed 'A file path became too long for Windows.'
Test-FailureCase 'locked file wins over port failure' @(
    'error: building zlib:x64-windows failed with: BUILD_FAILED',
    'LINK : fatal error LNK1104: cannot open file zlib.lib') $CmakeFailed 'A file the build needs to write is locked or not writable.'

# Unrelated failures are unchanged.
Test-FailureCase 'download failure' @('error: Downloading https://example.invalid/x.tar.gz failed') $CmakeFailed 'A third-party library could not be downloaded or built.'
Test-FailureCase 'compiler error in QuickSift' @("src\main.cpp(10): error C2065: 'x': undeclared identifier") "$CmakeFailed`nCommand: cmake --build build" 'The C++ compiler stopped with an error while building QuickSift.'

if ($Failures.Count -gt 0) {
    foreach ($Failure in $Failures) { Write-Host "FAIL: $Failure" }
    Write-Host "build-failure-help check failed: $($Failures.Count) problem(s)."
    exit 1
}
Write-Host 'build-failure-help check passed.'
