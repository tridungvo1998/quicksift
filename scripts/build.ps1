# CODE GUIDE: See CODE_GUIDE.md -> "Building, testing, and opening Visual Studio".
# OWNER: Windows prerequisite discovery, CMake configure/build, optional signing, and packaging.

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Debug',

    [ValidateSet('Auto', 'VisualStudio2026', 'VisualStudio2022')]
    [string]$VisualStudio = 'Auto',

    [switch]$Clean,
    [switch]$ConfigureOnly,
    [switch]$OpenVisualStudio,
    [switch]$VerifyBinary,
    [switch]$RequireSignature,
    [switch]$Package,
    [switch]$NoBootstrapVcpkg
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'QuickSift is a native Windows application. Run this script on Windows 10 or Windows 11.'
}

$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildHome = Join-Path $env:LOCALAPPDATA 'QuickSiftBuild'
$PowerShellExe = (Get-Process -Id $PID).Path
$BuildLogName = 'QuickSift-BuildLog-{0}.txt' -f (Get-Date -Format 'HHmmss')
$BuildLogPath = Join-Path $SourceDir $BuildLogName

# Capture the complete interactive build transcript beside the root build*.bat files.
# Start-Transcript preserves every Write-Host line as well as streamed native output.
$TranscriptStarted = $false
try {
    Start-Transcript -Path $BuildLogPath -Force | Out-Null
    $TranscriptStarted = $true
    Write-Host "Build log: $BuildLogPath" -ForegroundColor DarkGray
} catch {
    Write-Warning "Could not start build transcript at $BuildLogPath`: $($_.Exception.Message)"
}

function Write-Step([string]$Message) {
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $false)][string[]]$Arguments = @(),
        [Parameter(Mandatory = $false)][string]$WorkingDirectory = ''
    )

    $PreviousDirectory = Get-Location
    try {
        if ($WorkingDirectory) { Set-Location -LiteralPath $WorkingDirectory }
        Write-Host ("> " + $FilePath + ' ' + ($Arguments -join ' ')) -ForegroundColor DarkGray
        $FileArgumentIndex = [Array]::IndexOf($Arguments, '-File')
        # Stream native output live. Start-Transcript above captures every line
        # without retaining the full build transcript in PowerShell memory.
        & $FilePath @Arguments 2>&1 | ForEach-Object {
            Write-Host $_.ToString()
        }
        $ExitCode = $LASTEXITCODE
        if ($ExitCode -ne 0) {
            $RenderedArguments = ($Arguments | ForEach-Object {
                if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
            }) -join ' '
            $FailureTarget = $FilePath
            if ($FileArgumentIndex -ge 0 -and ($FileArgumentIndex + 1) -lt $Arguments.Count) {
                $FailureTarget = $Arguments[$FileArgumentIndex + 1]
            }
            throw "Command failed with exit code $ExitCode`: $FailureTarget`nCommand: $FilePath $RenderedArguments`nFull build transcript: $BuildLogPath"
        }
    } finally {
        if ($WorkingDirectory) { Set-Location -LiteralPath $PreviousDirectory }
    }
}

function Get-ExecutableFromPath([string]$Name) {
    $Command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($Command) { return $Command.Source }
    return $null
}

function Get-VisualStudioInstance {
    $VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $VsWhere -PathType Leaf)) {
        throw 'Visual Studio Installer (vswhere.exe) was not found. Install Visual Studio with the Desktop development with C++ workload.'
    }

    $Json = & $VsWhere -all -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -format json -utf8
    if ($LASTEXITCODE -ne 0 -or -not $Json) {
        throw 'No Visual Studio installation with the x64 C++ tools was found.'
    }

    $Instances = @(($Json -join [Environment]::NewLine) | ConvertFrom-Json)
    $Candidates = foreach ($Instance in $Instances) {
        $Version = [version]$Instance.installationVersion
        if ($Version.Major -in @(17, 18)) {
            [pscustomobject]@{
                Major = $Version.Major
                Version = $Version
                Path = [string]$Instance.installationPath
                DisplayName = [string]$Instance.displayName
            }
        }
    }

    if ($VisualStudio -eq 'VisualStudio2026') {
        $Candidates = @($Candidates | Where-Object { $_.Major -eq 18 })
    } elseif ($VisualStudio -eq 'VisualStudio2022') {
        $Candidates = @($Candidates | Where-Object { $_.Major -eq 17 })
    }

    $Selected = $Candidates | Sort-Object Version -Descending | Select-Object -First 1
    if (-not $Selected) {
        throw "The requested Visual Studio edition was not found. Requested: $VisualStudio."
    }
    return $Selected
}

function Get-CMakeVersion([string]$Path) {
    try {
        $FirstLine = (& $Path --version 2>$null | Select-Object -First 1)
        if ($FirstLine -match 'cmake version\s+([0-9]+(?:\.[0-9]+){1,3})') {
            return [version]$Matches[1]
        }
    } catch {
        return $null
    }
    return $null
}

function Get-CMakeExecutable($VsInstance) {
    $RequiredVersion = if ($VsInstance.Major -ge 18) { [version]'4.2.0' } else { [version]'3.24.0' }
    $CandidatePaths = [Collections.Generic.List[string]]::new()

    $PathCMake = Get-ExecutableFromPath 'cmake.exe'
    if ($PathCMake) { $CandidatePaths.Add($PathCMake) }

    $VsCMake = Join-Path $VsInstance.Path 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path -LiteralPath $VsCMake -PathType Leaf) { $CandidatePaths.Add($VsCMake) }

    $Best = $null
    foreach ($Candidate in ($CandidatePaths | Select-Object -Unique)) {
        $Version = Get-CMakeVersion $Candidate
        if ($Version -and $Version -ge $RequiredVersion) {
            if (-not $Best -or $Version -gt $Best.Version) {
                $Best = [pscustomobject]@{ Path = $Candidate; Version = $Version }
            }
        }
    }

    if (-not $Best) {
        throw "CMake $RequiredVersion or newer is required for $($VsInstance.DisplayName). Install the CMake tools component in Visual Studio or a current standalone CMake release."
    }
    return $Best
}

function Get-GitExecutable($VsInstance) {
    $Candidates = @(
        (Get-ExecutableFromPath 'git.exe'),
        (Join-Path $VsInstance.Path 'Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe'),
        (Join-Path $VsInstance.Path 'Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\mingw64\bin\git.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }

    return ($Candidates | Select-Object -First 1)
}

function Test-VcpkgRoot([string]$Path) {
    if (-not $Path) { return $false }
    return (Test-Path -LiteralPath (Join-Path $Path 'scripts\buildsystems\vcpkg.cmake') -PathType Leaf)
}

function Initialize-VcpkgRoot([string]$Path) {
    if (Test-Path -LiteralPath (Join-Path $Path 'vcpkg.exe') -PathType Leaf) {
        return (Resolve-Path $Path).Path
    }
    if ($NoBootstrapVcpkg) {
        throw "vcpkg exists at $Path but has not been bootstrapped. Rerun without -NoBootstrapVcpkg or run bootstrap-vcpkg.bat manually."
    }
    $Bootstrap = Join-Path $Path 'bootstrap-vcpkg.bat'
    if (-not (Test-Path -LiteralPath $Bootstrap -PathType Leaf)) {
        throw "The vcpkg checkout at $Path has no vcpkg.exe or bootstrap-vcpkg.bat."
    }
    Write-Step "Bootstrapping vcpkg at $Path"
    Invoke-Native $Bootstrap @('-disableMetrics') $Path
    if (-not (Test-Path -LiteralPath (Join-Path $Path 'vcpkg.exe') -PathType Leaf)) {
        throw "vcpkg bootstrap did not produce vcpkg.exe at $Path"
    }
    return (Resolve-Path $Path).Path
}

function Get-VcpkgRoot($VsInstance) {
    $Candidates = @(
        $env:VCPKG_ROOT,
        (Join-Path $VsInstance.Path 'VC\vcpkg'),
        (Join-Path $BuildHome 'tools\vcpkg')
    )

    foreach ($Candidate in $Candidates) {
        if (Test-VcpkgRoot $Candidate) { return (Initialize-VcpkgRoot $Candidate) }
    }

    if ($NoBootstrapVcpkg) {
        throw 'vcpkg was not found. Set VCPKG_ROOT or rerun without -NoBootstrapVcpkg to let the build script install a private copy.'
    }

    $Git = Get-GitExecutable $VsInstance
    if (-not $Git) {
        throw 'vcpkg is missing and Git could not be found. Install the Git for Windows component or set VCPKG_ROOT to an existing vcpkg checkout.'
    }

    $Destination = Join-Path $BuildHome 'tools\vcpkg'
    New-Item -ItemType Directory -Path (Split-Path $Destination -Parent) -Force | Out-Null
    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }

    Write-Step 'Installing a private vcpkg checkout'
    Invoke-Native $Git @('clone', '--filter=blob:none', 'https://github.com/microsoft/vcpkg.git', $Destination)
    Invoke-Native (Join-Path $Destination 'bootstrap-vcpkg.bat') @('-disableMetrics') $Destination

    if (-not (Test-VcpkgRoot $Destination)) {
        throw 'vcpkg bootstrap completed without producing the expected toolchain file.'
    }
    return (Initialize-VcpkgRoot $Destination)
}

function Get-DumpbinDirectory($VsInstance) {
    $ToolsRoot = Join-Path $VsInstance.Path 'VC\Tools\MSVC'
    if (-not (Test-Path -LiteralPath $ToolsRoot -PathType Container)) { return $null }
    $Dumpbin = Get-ChildItem -LiteralPath $ToolsRoot -Filter dumpbin.exe -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\bin\\Hostx64\\x64\\dumpbin\.exe$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($Dumpbin) { return $Dumpbin.DirectoryName }
    return $null
}

function Get-SignTool {
    $FromPath = Get-ExecutableFromPath 'signtool.exe'
    if ($FromPath) { return $FromPath }

    $KitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (-not (Test-Path -LiteralPath $KitsRoot -PathType Container)) { return $null }
    $Candidate = Get-ChildItem -LiteralPath $KitsRoot -Filter signtool.exe -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($Candidate) { return $Candidate.FullName }
    return $null
}

function Read-QuickSiftVersion {
    $Header = [IO.File]::ReadAllText((Join-Path $SourceDir 'src\version.h'), [Text.Encoding]::UTF8)
    if ($Header -notmatch '#define\s+QS_VERSION_STRING\s+"([^"]+)"') {
        throw 'Could not read QS_VERSION_STRING from src\version.h.'
    }
    $ProductVersion = $Matches[1]
    if ($ProductVersion -notmatch '^\d+\.\d+\.\d+\.\d+\s+(?:RC|Beta|Alpha)\d*$') {
        throw "QS_VERSION_STRING must use a numeric four-component version followed by a release label such as '0.9.3.0 RC'; got '$ProductVersion'."
    }
    return ($ProductVersion -replace '\s+', '-')
}

$Vs = Get-VisualStudioInstance
$CMake = Get-CMakeExecutable $Vs
$Generator = if ($Vs.Major -ge 18) { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
$VcpkgRoot = Get-VcpkgRoot $Vs
$BuildDir = Join-Path $BuildHome ("cmake-vs{0}" -f $Vs.Major)
$InstalledDir = Join-Path $BuildHome ("vcpkg_installed-vs{0}" -f $Vs.Major)
$SolutionPath = $null
$SolutionCandidates = if ($Vs.Major -ge 18) {
    @(
        (Join-Path $BuildDir 'QuickSift.slnx'),
        (Join-Path $BuildDir 'QuickSift.sln')
    )
} else {
    @(
        (Join-Path $BuildDir 'QuickSift.sln'),
        (Join-Path $BuildDir 'QuickSift.slnx')
    )
}
# Visual Studio is a multi-configuration generator; binaries live under Debug/, Release/, etc.
$ConfigurationOutputDir = Join-Path $BuildDir $Configuration
$ExecutablePath = Join-Path $ConfigurationOutputDir 'QuickSift.exe'

Write-Host "QuickSift build environment" -ForegroundColor Green
Write-Host "  Visual Studio : $($Vs.DisplayName) ($($Vs.Version))"
Write-Host "  CMake        : $($CMake.Version)"
Write-Host "  vcpkg        : $VcpkgRoot"
Write-Host "  Build folder : $BuildDir"
Write-Host "  Configuration: $Configuration"

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Write-Step 'Removing the previous generated solution and build files'
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
New-Item -ItemType Directory -Path $InstalledDir -Force | Out-Null

Write-Step 'Checking source structure and version consistency'
Invoke-Native $PowerShellExe @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', (Join-Path $SourceDir 'scripts\verify-source.ps1'),
    '-SourceDir', $SourceDir
)

Write-Step "Configuring the $Generator x64 solution"
$ConfigureArguments = @(
    '--fresh',
    '-S', $SourceDir,
    '-B', $BuildDir,
    '-G', $Generator,
    '-A', 'x64',
    "-DCMAKE_GENERATOR_INSTANCE=$($Vs.Path)",
    "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake')",
    "-DVCPKG_INSTALLED_DIR=$InstalledDir",
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static',
    '-DVCPKG_HOST_TRIPLET=x64-windows',
    '-DVCPKG_MANIFEST_INSTALL=ON',
    "-DVCPKG_OVERLAY_PORTS=$(Join-Path $SourceDir 'vcpkg_overlay_ports')",
    '-DQS_BUNDLE_CODECS=ON',
    '-DQS_WARNINGS_AS_ERRORS=ON'
)
try {
    Invoke-Native $CMake.Path $ConfigureArguments
} catch {
    $FailureText = $_.Exception.Message
    # Only retry when the diagnostics point to a stale/incomplete generated
    # CMake or vcpkg state. Real source/configuration errors fail immediately.
    if ($FailureText -notmatch '(?i)(vcpkg|ABI|installed tree|toolchain|package.*install|lock|corrupt|incomplete)') {
        throw
    }
    Write-Warning 'CMake configure looks like stale generated/vcpkg state. Recreating those directories and retrying once.'
    Remove-Item -LiteralPath $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $InstalledDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    New-Item -ItemType Directory -Path $InstalledDir -Force | Out-Null
    Invoke-Native $CMake.Path $ConfigureArguments
}

$SolutionPath = $SolutionCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $SolutionPath) {
    $Expected = $SolutionCandidates -join ', '
    throw "CMake configuration completed, but no generated Visual Studio solution was found. Checked: $Expected"
}

# Fail immediately if the generated Visual Studio project does not carry the
# converted version-resource object as a real build input. This guards against
# a valid-looking .rc/.res file being silently omitted from the final PE image.
$ApplicationProjectPath = Join-Path $BuildDir 'QuickSift.vcxproj'
if (-not (Test-Path -LiteralPath $ApplicationProjectPath -PathType Leaf)) {
    throw "CMake configuration did not produce the QuickSift project: $ApplicationProjectPath"
}
$ApplicationProjectText = [IO.File]::ReadAllText($ApplicationProjectPath, [Text.Encoding]::UTF8)
if ($ApplicationProjectText -notmatch 'QuickSiftVersion\.obj') {
    throw 'The generated QuickSift project does not link QuickSiftVersion.obj. Refusing to build an executable without version resources.'
}
if ($ApplicationProjectText -notmatch '(?i)DEPENDENTLOADFLAG(?::|%3A)(?:0x)?800') {
    throw 'The generated QuickSift project does not pass /DEPENDENTLOADFLAG:0x800 to link.exe.'
}

$GeneratedVersionResourcePath = Join-Path $BuildDir 'generated\QuickSiftVersion.rc'
if (-not (Test-Path -LiteralPath $GeneratedVersionResourcePath -PathType Leaf)) {
    throw "CMake configuration did not generate the QuickSift version resource: $GeneratedVersionResourcePath"
}
$GeneratedVersionResourceText = [IO.File]::ReadAllText($GeneratedVersionResourcePath, [Text.Encoding]::UTF8)
if ($GeneratedVersionResourceText -notmatch '(?m)^\s*1\s+VERSIONINFO\b') {
    throw 'The generated QuickSift VERSIONINFO resource is not numeric resource ID 1. Windows version APIs would not discover it.'
}

if (-not $ConfigureOnly) {
    Write-Step "Building QuickSift ($Configuration)"
    Invoke-Native $CMake.Path @('--build', $BuildDir, '--config', $Configuration, '--parallel')


    if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
        throw "Build completed without producing QuickSift.exe at $ExecutablePath"
    }

    if ($env:QS_SIGN_CERT_SHA1) {
        Write-Step 'Signing QuickSift.exe'
        $SignTool = Get-SignTool
        if (-not $SignTool) { throw 'QS_SIGN_CERT_SHA1 is set, but signtool.exe was not found.' }
        $SignArguments = @('sign', '/sha1', $env:QS_SIGN_CERT_SHA1, '/fd', 'SHA256')
        if ($env:QS_TIMESTAMP_URL) {
            $SignArguments += @('/tr', $env:QS_TIMESTAMP_URL, '/td', 'SHA256')
        } else {
            Write-Warning 'QS_TIMESTAMP_URL is not set. The signature will not be timestamped.'
        }
        $SignArguments += $ExecutablePath
        Invoke-Native $SignTool $SignArguments
        Invoke-Native $SignTool @('verify', '/pa', '/v', $ExecutablePath)
    }

    if ($Configuration -eq 'Release' -or $VerifyBinary -or $RequireSignature) {
        Write-Step 'Verifying Windows binary mitigations and version resources'
        $DumpbinDirectory = Get-DumpbinDirectory $Vs
        if (-not $DumpbinDirectory) { throw 'dumpbin.exe was not found in the selected Visual Studio installation.' }
        $PreviousPath = $env:PATH
        try {
            $env:PATH = "$DumpbinDirectory;$env:PATH"
            $VerifyArguments = @(
                '-NoProfile', '-ExecutionPolicy', 'Bypass',
                '-File', (Join-Path $SourceDir 'scripts\verify-binary.ps1'),
                '-Path', $ExecutablePath
            )
            if ($RequireSignature) { $VerifyArguments += '-RequireSignature' }
            Invoke-Native $PowerShellExe $VerifyArguments
        } finally {
            $env:PATH = $PreviousPath
        }
    }

    if ($Package) {
        Write-Step 'Creating the distributable ZIP and SHA-256 file'
        $Version = Read-QuickSiftVersion
        $PackageRoot = Join-Path $BuildHome 'packages'
        $StageDir = Join-Path $BuildHome 'package-stage'
        $ZipPath = Join-Path $PackageRoot ("QuickSift-{0}-windows-x64.zip" -f $Version)
        $HashPath = "$ZipPath.sha256"

        Remove-Item -LiteralPath $StageDir -Recurse -Force -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Path $StageDir -Force | Out-Null
        New-Item -ItemType Directory -Path $PackageRoot -Force | Out-Null

        Copy-Item -LiteralPath $ExecutablePath -Destination (Join-Path $StageDir 'QuickSift.exe')
        foreach ($Document in @('README.md', 'USER_GUIDE.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'SOURCE_CODE.txt', 'SECURITY.md')) {
            Copy-Item -LiteralPath (Join-Path $SourceDir $Document) -Destination $StageDir
        }

        Remove-Item -LiteralPath $ZipPath -Force -ErrorAction SilentlyContinue
        Compress-Archive -Path (Join-Path $StageDir '*') -DestinationPath $ZipPath -CompressionLevel Optimal
        $Hash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
        [IO.File]::WriteAllText($HashPath, "$Hash  $([IO.Path]::GetFileName($ZipPath))`r`n", [Text.UTF8Encoding]::new($false))
        Write-Host "Package: $ZipPath" -ForegroundColor Green
        Write-Host "SHA-256: $Hash" -ForegroundColor Green
    }

    Write-Host "`nBuild succeeded: $ExecutablePath" -ForegroundColor Green
}

if ($OpenVisualStudio) {
    Write-Step 'Opening the generated Visual Studio solution'
    $Devenv = Join-Path $Vs.Path 'Common7\IDE\devenv.exe'
    if (-not (Test-Path -LiteralPath $Devenv -PathType Leaf)) {
        throw "devenv.exe was not found in the selected Visual Studio installation: $Devenv"
    }
    Start-Process -FilePath $Devenv -ArgumentList @("`"$SolutionPath`"")
}

if ($ConfigureOnly) {
    Write-Host "`nVisual Studio solution ready: $SolutionPath" -ForegroundColor Green
}
