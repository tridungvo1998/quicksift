# OWNER: Deterministic Microsoft Store MSIX staging/manifest/package creation.
[CmdletBinding()]
param(
    [string]$IdentityName = '',
    [string]$Publisher = '',
    [string]$SourceUrl = '',
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($IdentityName)) {
    $IdentityName = if ($env:QS_MSIX_IDENTITY_NAME) { $env:QS_MSIX_IDENTITY_NAME } else { 'VoHoangTriDung.QuickSift' }
}
if ([string]::IsNullOrWhiteSpace($Publisher)) {
    $Publisher = if ($env:QS_MSIX_PUBLISHER) { $env:QS_MSIX_PUBLISHER } else { 'CN=AC70415F-6823-444B-A5C0-264E22E30568' }
}
if ([string]::IsNullOrWhiteSpace($SourceUrl)) {
    $SourceUrl = if ($env:QS_SOURCE_URL) { $env:QS_SOURCE_URL } else { 'https://github.com/tridungvo1998/quicksift/' }
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'MSIX packaging must run on Windows.'
}
if ([string]::IsNullOrWhiteSpace($IdentityName)) {
    throw 'MSIX IdentityName is required. Copy the Package/Identity Name exactly from the Microsoft Store Partner Center product identity.'
}
if ([string]::IsNullOrWhiteSpace($Publisher)) {
    throw 'MSIX Publisher is required. Copy the Publisher value exactly from Microsoft Store Partner Center (for example CN=...).'
}
if ([string]::IsNullOrWhiteSpace($SourceUrl) -or -not $SourceUrl.StartsWith('https://', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Corresponding source URL must be an HTTPS URL for the exact GPL-2.0-or-later source of this release.'
}

$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildHome = Join-Path $env:LOCALAPPDATA 'QuickSiftBuild'
$PackageRoot = Join-Path $BuildHome 'packages'
$StageDir = Join-Path $BuildHome 'msix-stage'

$VersionHeader = [IO.File]::ReadAllText((Join-Path $SourceDir 'src\version.h'), [Text.Encoding]::UTF8)
if ($VersionHeader -notmatch '#define\s+QS_VERSION_STRING\s+"([^"]+)"') {
    throw 'Could not read QS_VERSION_STRING from src\version.h.'
}
$ProductVersion = $Matches[1]
if ($VersionHeader -notmatch '#define\s+QS_VERSION_FILE_STRING\s+"([^"]+)"') {
    throw 'Could not read QS_VERSION_FILE_STRING from src\version.h.'
}
$MsixVersion = $Matches[1]
if ($MsixVersion -notmatch '^\d+\.\d+\.\d+\.0$') {
    throw "MSIX version must have a zero revision component; got '$MsixVersion'."
}
$ReleaseSlug = $ProductVersion -replace '\s+', '-'
$PackagePath = Join-Path $PackageRoot ("QuickSift-{0}-x64.msix" -f $ReleaseSlug)
$HashPath = "$PackagePath.sha256"

function Invoke-Native {
    param([string]$FilePath, [string[]]$Arguments)
    Write-Host ("> " + $FilePath + ' ' + ($Arguments -join ' ')) -ForegroundColor DarkGray
    $Output = @(& $FilePath @Arguments 2>&1)
    $ExitCode = $LASTEXITCODE
    $Output | Out-Host
    if ($ExitCode -ne 0) {
        $Tail = @($Output | Select-Object -Last 30 | ForEach-Object { $_.ToString() })
        throw "Command failed with exit code $ExitCode`: $FilePath`n$($Tail -join [Environment]::NewLine)"
    }
}

function Find-WindowsSdkTool([string]$Name) {
    $FromPath = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($FromPath) { return $FromPath.Source }
    $Root = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return $null }
    $Candidate = Get-ChildItem -LiteralPath $Root -Filter $Name -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($Candidate) { return $Candidate.FullName }
    return $null
}

if (-not $SkipBuild) {
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $SourceDir 'scripts\build.ps1') `
        -Configuration Release -Clean -VerifyBinary
    if ($LASTEXITCODE -ne 0) { throw "QuickSift Release build failed with exit code $LASTEXITCODE." }
}

$Executable = Get-ChildItem -LiteralPath $BuildHome -Filter QuickSift.exe -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\cmake-vs(?:17|18)\\Release\\QuickSift\.exe$' } |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1
if (-not $Executable) { throw 'Could not locate the freshly built Release QuickSift.exe under %LOCALAPPDATA%\QuickSiftBuild.' }

$MakeAppx = Find-WindowsSdkTool 'makeappx.exe'
if (-not $MakeAppx) { throw 'makeappx.exe was not found. Install a Windows 10/11 SDK.' }

Remove-Item -LiteralPath $StageDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path (Join-Path $StageDir 'Assets') -Force | Out-Null
New-Item -ItemType Directory -Path $PackageRoot -Force | Out-Null
Copy-Item -LiteralPath $Executable.FullName -Destination (Join-Path $StageDir 'QuickSift.exe')
foreach ($Document in @('LICENSE', 'THIRD_PARTY_NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $SourceDir $Document) -Destination $StageDir
}
Copy-Item -Path (Join-Path $SourceDir 'packaging\msix\Assets\*') -Destination (Join-Path $StageDir 'Assets')

$IdentityEscaped = [Security.SecurityElement]::Escape($IdentityName)
$PublisherEscaped = [Security.SecurityElement]::Escape($Publisher)
$ManifestTemplate = [IO.File]::ReadAllText((Join-Path $SourceDir 'packaging\msix\AppxManifest.xml.in'), [Text.Encoding]::UTF8)
$Manifest = $ManifestTemplate.Replace('@QS_MSIX_IDENTITY_NAME@', $IdentityEscaped).Replace('@QS_MSIX_PUBLISHER@', $PublisherEscaped)
if ($Manifest -notmatch '<Identity\b[^>]*\bVersion="([^"]+)"') {
    throw 'MSIX manifest template does not declare an Identity Version.'
}
if ($Matches[1] -ne $MsixVersion) {
    throw "MSIX manifest version '$($Matches[1])' does not match canonical file version '$MsixVersion' from src\version.h."
}
[IO.File]::WriteAllText((Join-Path $StageDir 'AppxManifest.xml'), $Manifest, [Text.UTF8Encoding]::new($false))

$SourceNotice = @"
QuickSift $ProductVersion — Corresponding Source Code

License: GPL-2.0-or-later
Exact corresponding source: $SourceUrl
Project website: https://quicksift.pages.dev/
"@
[IO.File]::WriteAllText((Join-Path $StageDir 'SOURCE_CODE.txt'), $SourceNotice, [Text.UTF8Encoding]::new($false))

Remove-Item -LiteralPath $PackagePath -Force -ErrorAction SilentlyContinue
Invoke-Native $MakeAppx @('pack', '/d', $StageDir, '/p', $PackagePath, '/o')

if ($env:QS_SIGN_CERT_SHA1) {
    $SignTool = Find-WindowsSdkTool 'signtool.exe'
    if (-not $SignTool) { throw 'QS_SIGN_CERT_SHA1 is set but signtool.exe was not found.' }
    $SignArgs = @('sign', '/sha1', $env:QS_SIGN_CERT_SHA1, '/fd', 'SHA256')
    if ($env:QS_TIMESTAMP_URL) { $SignArgs += @('/tr', $env:QS_TIMESTAMP_URL, '/td', 'SHA256') }
    $SignArgs += $PackagePath
    Invoke-Native $SignTool $SignArgs
    Invoke-Native $SignTool @('verify', '/pa', '/v', $PackagePath)
} else {
    Write-Host 'MSIX is unsigned. This is appropriate for Microsoft Store submission; the Store re-signs certified MSIX packages. For direct sideload testing, sign it with a certificate trusted on the test device.' -ForegroundColor Yellow
}

$Hash = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($HashPath, "$Hash  $([IO.Path]::GetFileName($PackagePath))`r`n", [Text.UTF8Encoding]::new($false))
Write-Host "MSIX: $PackagePath" -ForegroundColor Green
Write-Host "SHA-256: $Hash" -ForegroundColor Green
