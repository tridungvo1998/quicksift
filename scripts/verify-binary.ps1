[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Path,
    [switch]$RequireSignature
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Path = (Resolve-Path $Path).Path
if ([IO.Path]::GetExtension($Path) -ne '.exe') { throw 'Binary verification expects an .exe file.' }

$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$VersionHeader = Get-Content (Join-Path $SourceDir 'src\version.h') -Raw
if ($VersionHeader -notmatch '#define\s+QS_VERSION_STRING\s+"([^"]+)"') { throw 'Could not read expected product version.' }
$ExpectedProductVersion = $Matches[1]
if ($VersionHeader -notmatch '#define\s+QS_VERSION_FILE_STRING\s+"([^"]+)"') { throw 'Could not read expected file version.' }
$ExpectedFileVersion = $Matches[1]
$VersionInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
if ($VersionInfo.FileVersion -ne $ExpectedFileVersion -or $VersionInfo.ProductVersion -ne $ExpectedProductVersion) {
    throw "Version resource mismatch. Expected FileVersion=$ExpectedFileVersion ProductVersion=$ExpectedProductVersion; FileVersion=$($VersionInfo.FileVersion), ProductVersion=$($VersionInfo.ProductVersion)."
}
if ($VersionInfo.CompanyName -ne 'Vo Hoang Tri Dung' -or
    $VersionInfo.FileDescription -ne 'QuickSift — Fast Photo Culling for Windows' -or
    $VersionInfo.LegalCopyright -ne 'Copyright © 2026 Vo Hoang Tri Dung' -or
    $VersionInfo.OriginalFilename -ne 'QuickSift.exe' -or
    $VersionInfo.ProductName -ne 'QuickSift' -or
    $VersionInfo.Comments -ne 'https://quicksift.pages.dev/') {
    throw 'Release identity/version resource metadata does not match the source-defined QuickSift release contract.'
}

$Dumpbin = (Get-Command dumpbin.exe -ErrorAction Stop).Source
$Headers = (& $Dumpbin /nologo /headers /loadconfig $Path 2>&1 | Out-String)
function Get-PeDependentLoadFlags(
    [Parameter(Mandatory = $true)][string]$ImagePath
) {
    # Read IMAGE_LOAD_CONFIG_DIRECTORY directly from the PE image. DUMPBIN's
    # presentation of this field has changed between MSVC releases, while the
    # PE/COFF layout is stable and documented. Offsets below are decimal:
    # DependentLoadFlags is at 54 in PE32 and 78 in PE32+ load configuration.
    $Stream = [IO.File]::Open(
        $ImagePath,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read
    )
    $Reader = [IO.BinaryReader]::new($Stream)
    try {
        if ($Stream.Length -lt 256) { throw 'The executable is too small to be a valid PE image.' }

        $Stream.Position = 0x3c
        [UInt32]$PeOffset = $Reader.ReadUInt32()
        if ([UInt64]$PeOffset + 24 -gt [UInt64]$Stream.Length) {
            throw 'The PE header offset is outside the executable.'
        }

        $Stream.Position = $PeOffset
        if ($Reader.ReadUInt32() -ne 0x00004550) { throw 'The executable has no PE signature.' }

        [UInt16]$Machine = $Reader.ReadUInt16()
        [UInt16]$NumberOfSections = $Reader.ReadUInt16()
        $Stream.Position = [Int64]$PeOffset + 20
        [UInt16]$SizeOfOptionalHeader = $Reader.ReadUInt16()
        [Int64]$OptionalHeaderOffset = [Int64]$PeOffset + 24
        if ([UInt64]$OptionalHeaderOffset + $SizeOfOptionalHeader -gt [UInt64]$Stream.Length) {
            throw 'The PE optional header extends beyond the executable.'
        }

        $Stream.Position = $OptionalHeaderOffset
        [UInt16]$Magic = $Reader.ReadUInt16()
        if ($Magic -eq 0x10b) {
            [int]$DataDirectoryOffset = 96
            [int]$NumberOfDirectoriesOffset = 92
            [int]$DependentLoadFlagsOffset = 54
            $PeKind = 'PE32'
        } elseif ($Magic -eq 0x20b) {
            [int]$DataDirectoryOffset = 112
            [int]$NumberOfDirectoriesOffset = 108
            [int]$DependentLoadFlagsOffset = 78
            $PeKind = 'PE32+'
        } else {
            throw ('Unsupported PE optional-header magic: 0x{0:X4}.' -f $Magic)
        }

        # Data-directory index 10 is IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG.
        [int]$LoadConfigDirectoryIndex = 10
        [int]$RequiredOptionalHeaderBytes = $DataDirectoryOffset + (($LoadConfigDirectoryIndex + 1) * 8)
        if ($SizeOfOptionalHeader -lt $RequiredOptionalHeaderBytes) {
            throw 'The PE optional header has no load-configuration directory entry.'
        }

        $Stream.Position = $OptionalHeaderOffset + $NumberOfDirectoriesOffset
        [UInt32]$NumberOfDirectories = $Reader.ReadUInt32()
        if ($NumberOfDirectories -le $LoadConfigDirectoryIndex) {
            throw 'The PE image does not publish a load-configuration directory.'
        }

        $Stream.Position = $OptionalHeaderOffset + 60
        [UInt32]$SizeOfHeaders = $Reader.ReadUInt32()

        $Stream.Position = $OptionalHeaderOffset + $DataDirectoryOffset + ($LoadConfigDirectoryIndex * 8)
        [UInt32]$LoadConfigRva = $Reader.ReadUInt32()
        [UInt32]$LoadConfigDirectorySize = $Reader.ReadUInt32()
        if ($LoadConfigRva -eq 0 -or $LoadConfigDirectorySize -eq 0) {
            throw 'The PE image has an empty load-configuration directory.'
        }
        if ($LoadConfigDirectorySize -lt ($DependentLoadFlagsOffset + 2)) {
            throw ('The PE load-configuration directory is too small ({0} bytes) to contain DependentLoadFlags.' -f $LoadConfigDirectorySize)
        }

        [Int64]$LoadConfigFileOffset = -1
        if ($LoadConfigRva -lt $SizeOfHeaders) {
            $LoadConfigFileOffset = $LoadConfigRva
        } else {
            [Int64]$SectionTableOffset = $OptionalHeaderOffset + $SizeOfOptionalHeader
            if ([UInt64]$SectionTableOffset + ([UInt64]$NumberOfSections * 40) -gt [UInt64]$Stream.Length) {
                throw 'The PE section table extends beyond the executable.'
            }

            for ([int]$Index = 0; $Index -lt $NumberOfSections; ++$Index) {
                [Int64]$SectionOffset = $SectionTableOffset + ($Index * 40)
                $Stream.Position = $SectionOffset + 8
                [UInt32]$VirtualSize = $Reader.ReadUInt32()
                [UInt32]$VirtualAddress = $Reader.ReadUInt32()
                [UInt32]$SizeOfRawData = $Reader.ReadUInt32()
                [UInt32]$PointerToRawData = $Reader.ReadUInt32()

                [UInt64]$SectionSpan = $VirtualSize
                if ([UInt64]$SizeOfRawData -gt $SectionSpan) { $SectionSpan = $SizeOfRawData }
                [UInt64]$Rva64 = $LoadConfigRva
                [UInt64]$SectionStart = $VirtualAddress
                if ($Rva64 -ge $SectionStart -and $Rva64 -lt ($SectionStart + $SectionSpan)) {
                    [UInt64]$Delta = $Rva64 - $SectionStart
                    if ($Delta + [UInt64]($DependentLoadFlagsOffset + 2) -gt [UInt64]$SizeOfRawData) {
                        throw 'The PE load configuration points into virtual section padding rather than file data.'
                    }
                    $LoadConfigFileOffset = [Int64]([UInt64]$PointerToRawData + $Delta)
                    break
                }
            }
        }

        if ($LoadConfigFileOffset -lt 0) {
            throw ('Could not map load-configuration RVA 0x{0:X8} to the executable file.' -f $LoadConfigRva)
        }
        if ([UInt64]$LoadConfigFileOffset + [UInt64]($DependentLoadFlagsOffset + 2) -gt [UInt64]$Stream.Length) {
            throw 'The mapped PE load-configuration field extends beyond the executable.'
        }

        $Stream.Position = $LoadConfigFileOffset
        [UInt32]$LoadConfigStructureSize = $Reader.ReadUInt32()
        if ($LoadConfigStructureSize -lt ($DependentLoadFlagsOffset + 2)) {
            throw ('The IMAGE_LOAD_CONFIG_DIRECTORY structure is too small ({0} bytes) to contain DependentLoadFlags.' -f $LoadConfigStructureSize)
        }

        $Stream.Position = $LoadConfigFileOffset + $DependentLoadFlagsOffset
        [UInt16]$Flags = $Reader.ReadUInt16()
        return [pscustomobject]@{
            Flags = $Flags
            PeKind = $PeKind
            Machine = $Machine
            DirectoryRva = $LoadConfigRva
            DirectorySize = $LoadConfigDirectorySize
            StructureSize = $LoadConfigStructureSize
            FileOffset = $LoadConfigFileOffset
        }
    } finally {
        $Reader.Dispose()
        $Stream.Dispose()
    }
}

$Checks = [ordered]@{
    'x64 machine target' = 'machine \(x64\)|8664 machine'
    'Dynamic base (ASLR)' = 'Dynamic base'
    'High-entropy ASLR' = 'High Entropy Virtual Addresses'
    'NX compatibility (DEP)' = 'NX compatible'
    'Control Flow Guard image flag' = 'Guard CF'
    'CFG instrumentation/load configuration' = 'CF Instrumented|FID table present|Guard CF Function Table'
    'CET compatibility' = 'CET compatible'
    'Security cookie metadata' = 'Security Cookie'
}
$Missing = [Collections.Generic.List[string]]::new()
foreach ($Item in $Checks.GetEnumerator()) {
    if ($Headers -notmatch $Item.Value) { $Missing.Add($Item.Key) }
    else { Write-Host "PASS: $($Item.Key)" }
}
try {
    $PeLoadConfiguration = Get-PeDependentLoadFlags -ImagePath $Path
    if (($PeLoadConfiguration.Flags -band 0x800) -eq 0x800) {
        Write-Host ('PASS: Restricted dependent DLL loading (PE load-config flags = 0x{0:X4})' -f $PeLoadConfiguration.Flags)
    } else {
        Write-Warning ('PE load-config DependentLoadFlags is 0x{0:X4}; required bit 0x0800 is absent.' -f $PeLoadConfiguration.Flags)
        $Missing.Add('Restricted dependent DLL loading')
    }
} catch {
    Write-Warning "Could not read DependentLoadFlags directly from the PE load configuration: $($_.Exception.Message)"
    $DependentLoadFieldPattern = '(?i)Dependent\s*Load\s*Flags?|DependentLoadFlags'
    $ReportedDependentLoadLines = @(
        ($Headers -split "`r?`n") | Where-Object { $_ -match $DependentLoadFieldPattern }
    )
    if ($ReportedDependentLoadLines.Count -gt 0) {
        Write-Warning "DUMPBIN dependent-load field: $($ReportedDependentLoadLines -join ' | ')"
    }
    $Missing.Add('Restricted dependent DLL loading')
}
if ($Missing.Count -gt 0) { throw "Missing PE mitigations: $($Missing -join ', ')" }

if ($Headers -match 'EH Continuation') {
    Write-Host 'PASS: optional EH continuation metadata is present.'
} else {
    Write-Host 'INFO: EH continuation metadata is not required while bundled static dependencies are not EHCONT-compatible.'
}


$Signature = Get-AuthenticodeSignature $Path
if ($Signature.Status -eq 'Valid') {
    Write-Host "PASS: Authenticode signature is valid ($($Signature.SignerCertificate.Subject))."
} elseif ($RequireSignature) {
    throw "Authenticode signature is required but status is $($Signature.Status)."
} else {
    Write-Warning "Authenticode signature is not valid/present (status: $($Signature.Status))."
}

Write-Host "Binary verification passed for QuickSift $ExpectedProductVersion."
