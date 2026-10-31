# CODE GUIDE: See CODE_GUIDE.md -> "Building, testing, and opening Visual Studio".
# OWNER: Plain-language build failure messages. Dot-sourced by build.ps1,
# verify-source.ps1, verify-binary.ps1 and package-msix.ps1; each script's trap calls
# Write-QuickSiftFailure, which prints:
#   Build failed: <one-sentence cause>
#   What to do: <1-3 concrete steps>
#   Technical detail: <short original error>
# It never changes a successful build and never changes the exit code (the trap
# still exits non-zero). Keep this file ASCII: Windows PowerShell 5.1 reads
# BOM-less scripts in the ANSI code page.

$script:QuickSiftFailureHints = New-Object 'System.Collections.Generic.List[string]'
$script:QuickSiftFailureHintLimit = 12

# Native output lines worth remembering for the explanation. Only matching lines
# are kept (at most QuickSiftFailureHintLimit); all output is still streamed live.
$script:QuickSiftHintPattern = '(?i)(\berror\b|fatal|not found|could not find|cannot find|too long|MSB\d{4}|LNK\d{4}|\bC\d{4}\b|SignTool|python|Windows SDK|Windows Kits)'

function Add-QuickSiftFailureHint([string]$Line) {
    if ($null -eq $Line -or $script:QuickSiftFailureHints.Count -ge $script:QuickSiftFailureHintLimit) { return }
    if ($Line -notmatch $script:QuickSiftHintPattern) { return }
    $Trimmed = $Line.Trim()
    if ($Trimmed.Length -gt 300) { $Trimmed = $Trimmed.Substring(0, 300) + '...' }
    $script:QuickSiftFailureHints.Add($Trimmed)
}

function New-QuickSiftFailureRule([string]$Pattern, [string]$Cause, [string[]]$Steps) {
    [pscustomobject]@{ Pattern = $Pattern; Cause = $Cause; Steps = $Steps }
}

# Ordered: the first matching rule explains the failure.
$script:QuickSiftFailureRules = @(
    (New-QuickSiftFailureRule '(?i)native Windows application|must run on Windows' `
        'These build scripts only run on Windows 10 or Windows 11.' `
        @('Copy the QuickSift source folder to a Windows 10 or 11 PC and run build.bat there.')),
    (New-QuickSiftFailureRule '(?i)verify-source\.ps1' `
        'The source check found a problem in the QuickSift source, so nothing was compiled.' `
        @('Scroll up to the first "Build failed" message: it was printed by the source check and names the file and the rule.',
          'If you changed the source, fix that file (CODE_GUIDE.md section 16 explains each rule) and run the build again.',
          'If you did not change anything, download or unzip a fresh copy of the source.')),
    (New-QuickSiftFailureRule '(?i)verify-binary\.ps1' `
        'The finished QuickSift.exe did not pass the release safety check.' `
        @('Scroll up to the first "Build failed" message: it was printed by the binary check and says what was wrong.',
          'Run build-release.cmd again; it starts from a clean build folder.')),
    (New-QuickSiftFailureRule '(?i)scripts\\build\.ps1|QuickSift Release build failed' `
        'The Release build that the MSIX package needs did not finish.' `
        @('Scroll up to the first "Build failed" message: it explains why the build stopped.',
          'Fix that problem, then run package-msix.bat again.')),
    (New-QuickSiftFailureRule '(?i)path too long|PathTooLong|too long for|MSB3491|MSB3553|longer than 260|max(imum)? path|filename or extension is too long|fully qualified file name must be less than' `
        'A file path became too long for Windows.' `
        @('Move the QuickSift source folder to a short path, for example C:\src\QuickSift, and run the build again.',
          'Or turn on Windows long path support (the "Enable Win32 long paths" policy), restart, and run the build again.')),
    (New-QuickSiftFailureRule '(?i)Windows SDK|Windows Kits|makeappx\.exe was not found|No CMAKE_(C|CXX)_COMPILER|CMAKE_(C|CXX)_COMPILER could not be found|cannot open (include|source) file: .?(windows|kernel32|winsdkver)|LNK1181: cannot open input file .?kernel32' `
        'The Windows SDK (part of the Visual Studio C++ tools) is missing.' `
        @('Open the Visual Studio Installer, choose Modify, and tick "Windows 11 SDK" (or a Windows 10 SDK) under Individual components.',
          'Close this window, open a new one, and run the build again.')),
    (New-QuickSiftFailureRule '(?i)python.*(not found|could not be found|was not found|is not recognized|missing)|could not find python|PYTHON_EXECUTABLE' `
        'A library build step needs Python, and Python was not found.' `
        @('Install Python 3 from https://www.python.org/downloads/ and tick "Add python.exe to PATH" (or run: winget install Python.Python.3.12).',
          'Close this window, open a new one, and run the build again.')),
    (New-QuickSiftFailureRule '(?i)pwsh|powershell(\.exe)?.*(not recognized|not found|cannot be found)' `
        'PowerShell could not be started.' `
        @('Windows 10 and 11 include Windows PowerShell; make sure C:\Windows\System32\WindowsPowerShell\v1.0 is on PATH.',
          'If it is missing, repair Windows (run "sfc /scannow" as administrator) and run the build again.')),
    (New-QuickSiftFailureRule '(?i)requested Visual Studio edition was not found' `
        'The Visual Studio version you asked for is not installed.' `
        @('Run the build without the -VisualStudio option to use the newest installed Visual Studio.',
          'Or install that Visual Studio version with the "Desktop development with C++" workload.')),
    (New-QuickSiftFailureRule '(?i)vswhere|No Visual Studio installation|devenv\.exe was not found|dumpbin\.exe was not found' `
        'Visual Studio with the C++ tools is not installed, or is missing a part QuickSift needs.' `
        @('Install Visual Studio 2022 or 2026 (the free Community edition works) from https://visualstudio.microsoft.com/.',
          'In the Visual Studio Installer, tick the "Desktop development with C++" workload, then run the build again.')),
    (New-QuickSiftFailureRule '(?i)CMake [\d.]+ or newer is required|cmake(\.exe)?.*(not found|is not recognized)' `
        'CMake is missing or too old.' `
        @('Open the Visual Studio Installer, choose Modify, and tick "C++ CMake tools for Windows" under Individual components.',
          'Or install a current CMake from https://cmake.org/download/, then open a new window and run the build again.')),
    (New-QuickSiftFailureRule '(?i)Git could not be found|vcpkg was not found|not been bootstrapped|no vcpkg\.exe|vcpkg bootstrap' `
        'The vcpkg library manager could not be set up.' `
        @('Install "Git for Windows" (Visual Studio Installer, Individual components) and run the build again without -NoBootstrapVcpkg.',
          'Or set the VCPKG_ROOT environment variable to an existing, bootstrapped vcpkg folder.')),
    (New-QuickSiftFailureRule '(?i)signtool|SignTool Error|QS_SIGN_CERT_SHA1|certificate|Authenticode signature is required' `
        'Code signing failed: the signing certificate or signtool.exe is not available.' `
        @('If you do not need a signed build, clear the setting (run: set QS_SIGN_CERT_SHA1=) and build again.',
          'Otherwise import the certificate into your user certificate store, check that QS_SIGN_CERT_SHA1 is its SHA-1 thumbprint, and install the Windows SDK (it provides signtool.exe).')),
    (New-QuickSiftFailureRule '(?i)MSIX IdentityName|MSIX Publisher|source URL must be|manifest version' `
        'The Microsoft Store package settings are incomplete or do not match this release.' `
        @('Set QS_MSIX_IDENTITY_NAME, QS_MSIX_PUBLISHER and QS_SOURCE_URL to the values from Partner Center and your source download page.',
          'Run package-msix.bat again.')),
    (New-QuickSiftFailureRule '(?i)QS_VERSION' `
        'The version number in src\version.h is not in the expected format.' `
        @('Restore src\version.h from a fresh copy of the source, or fix QS_VERSION_STRING to look like "0.9.6.0 RC".')),
    (New-QuickSiftFailureRule '(?i)being used by another process|Access to the path .* is denied|access is denied|LNK1104|LNK1168' `
        'A file the build needs to write is locked or not writable.' `
        @('Close QuickSift and Visual Studio (and any window showing the build folder), then run the build again.',
          'If it keeps happening, restart Windows and try once more.')),
    (New-QuickSiftFailureRule '(?i)error: building \S+ failed|vcpkg install failed|failed to download|Downloading \S+ failed|error: .*vcpkg-tool' `
        'A third-party library could not be downloaded or built.' `
        @('Check your internet connection (and any proxy or antivirus that blocks downloads).',
          'Run build-release.cmd again; it starts from a clean build folder.',
          'If it still fails, delete %LOCALAPPDATA%\QuickSiftBuild and build once more.')),
    (New-QuickSiftFailureRule '(?i)error C\d{4}|error LNK\d{4}|error MSB\d{4}|\[build step\]' `
        'The C++ compiler stopped with an error while building QuickSift.' `
        @('Scroll up to the first line that contains "error": it names the file and the line.',
          'If you changed the source, fix that line and run the build again; if you did not, use a fresh copy of the source.')),
    (New-QuickSiftFailureRule '(?i)CMake configuration|cmake' `
        'CMake could not prepare the Visual Studio project.' `
        @('Run build-release.cmd again; it starts from a clean build folder.',
          'If it still fails, scroll up to the first CMake Error message, or delete %LOCALAPPDATA%\QuickSiftBuild and try once more.'))
)

$script:QuickSiftSourceCheckRule = New-QuickSiftFailureRule '' `
    'The source check found a problem in the QuickSift source.' `
    @('Read the technical detail below: it names the file and the rule that failed.',
      'If you changed the source, fix that file (CODE_GUIDE.md section 16 explains each rule); otherwise use a fresh copy of the source.')
$script:QuickSiftBinaryCheckRule = New-QuickSiftFailureRule '' `
    'The built QuickSift.exe did not pass the release safety check.' `
    @('Run build-release.cmd again; it starts from a clean build folder.',
      'If the same check fails again, share the technical detail below and the build log when you ask for help.')
$script:QuickSiftUnknownRule = New-QuickSiftFailureRule '' `
    'Something unexpected stopped the build.' `
    @('Run the build again; a temporary problem (a locked file, a network hiccup) often clears.',
      'If it fails the same way, share the technical detail below and the build log when you ask for help.')

function Get-QuickSiftFailureHelp([string]$Message, [string]$Context = 'Build') {
    # The command line repeats tool paths (vcpkg, toolchain); classify on the
    # error itself plus the remembered native output lines, not on the arguments.
    $Lines = @($Message -split "`r?`n" | Where-Object { $_ -and $_ -notmatch '^\s*Command:' })
    $Text = (($Lines + @($script:QuickSiftFailureHints)) -join "`n")
    if ($Message -match '(?m)^\s*Command:.*\s--build\s') { $Text += "`n[build step]" }
    if ($Context -eq 'Source' -and $Text -notmatch '(?i)native Windows application') { return $script:QuickSiftSourceCheckRule }
    if ($Context -eq 'Binary') { return $script:QuickSiftBinaryCheckRule }
    foreach ($Rule in $script:QuickSiftFailureRules) {
        if ($Text -match $Rule.Pattern) { return $Rule }
    }
    return $script:QuickSiftUnknownRule
}

function Write-QuickSiftFailure($ErrorRecord, [string]$Context = 'Build', [string]$LogPath = '') {
    try {
        $Message = if ($ErrorRecord -and $ErrorRecord.Exception) { [string]$ErrorRecord.Exception.Message } else { [string]$ErrorRecord }
        $Help = Get-QuickSiftFailureHelp $Message $Context
        $Steps = @($Help.Steps)
        $DetailLines = @($Message -split "`r?`n" | Where-Object { $_ -and $_ -notmatch '^\s*(Command|Full build transcript):' } | Select-Object -First 2)
        $FirstHint = @($script:QuickSiftFailureHints | Where-Object { $_ -match '(?i)\berror\b|fatal|too long' } | Select-Object -First 1)
        if ($FirstHint.Count -gt 0) { $DetailLines += $FirstHint[0] }
        if ($ErrorRecord -and $ErrorRecord.InvocationInfo -and $ErrorRecord.InvocationInfo.ScriptName) {
            $DetailLines += ('({0}, line {1})' -f [IO.Path]::GetFileName($ErrorRecord.InvocationInfo.ScriptName), $ErrorRecord.InvocationInfo.ScriptLineNumber)
        }
        Write-Host ''
        Write-Host ("Build failed: " + $Help.Cause) -ForegroundColor Red
        if ($Steps.Count -eq 1) {
            Write-Host ("What to do: " + $Steps[0]) -ForegroundColor Yellow
        } else {
            Write-Host 'What to do:' -ForegroundColor Yellow
            for ($Index = 0; $Index -lt $Steps.Count; $Index++) {
                Write-Host ('  {0}. {1}' -f ($Index + 1), $Steps[$Index]) -ForegroundColor Yellow
            }
        }
        if ($DetailLines.Count -gt 0) {
            Write-Host ("Technical detail: " + ($DetailLines -join "`n  ")) -ForegroundColor DarkGray
        }
        if ($LogPath) { Write-Host "Build log (share it when asking for help): $LogPath" -ForegroundColor DarkGray }
    } catch {
        # Never hide the original failure behind a helper problem.
        Write-Host ("Build failed: " + [string]$ErrorRecord) -ForegroundColor Red
    }
}
