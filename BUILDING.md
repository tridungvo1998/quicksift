# Building QuickSift 0.9

QuickSift is a native x64 Windows application. The project uses CMake as the single build definition and can be opened as a generated Visual Studio solution or directly as a CMake folder.

Start with [`CODE_GUIDE.md`](CODE_GUIDE.md) when changing the build or source layout.

## Required Windows tools

- Windows 10 or Windows 11, x64
- Visual Studio 2026 or Visual Studio 2022
- **Desktop development with C++** workload
- CMake tools for Windows
- Git for Windows when the build script needs to install vcpkg automatically
- PowerShell 5.1 or later

The included [`.vsconfig`](.vsconfig) lets Visual Studio Installer select the required workload and CMake component.

Visual Studio 2026 requires CMake 4.2 or newer for its native generator. Visual Studio 2022 requires CMake 3.24 or newer. The build script checks this before configuring anything.

## Easiest development build

Double-click:

```text
build.cmd
```

That creates a strict **Debug** build, installs a private vcpkg checkout under `%LOCALAPPDATA%\QuickSiftBuild` when necessary, verifies the source, and builds all production targets.

Equivalent command:

```bat
build.cmd -Configuration Debug
```

The executable is written outside the source tree:

```text
%LOCALAPPDATA%\QuickSiftBuild\cmake-vs18\Release\QuickSift.exe
```

For a Debug build, replace `Release` with `Debug`. For Visual Studio 2022, the folder ends in `cmake-vs17`.

## Open the project in Visual Studio

Double-click:

```text
open-in-visual-studio.cmd
```

The script:

1. Detects Visual Studio 2026 or 2022 with the x64 C++ tools.
2. Finds a compatible CMake installation.
3. Finds or installs vcpkg.
4. Runs source verification.
5. Generates `QuickSift.slnx` with Visual Studio 2026 or `QuickSift.sln` with Visual Studio 2022 under `%LOCALAPPDATA%\QuickSiftBuild`.
6. Detects the generated solution format and opens it in the selected Visual Studio installation.

`QuickSift` is configured as the Visual Studio startup project, so **F5** launches the application rather than `ALL_BUILD`.

Generated solution and dependency files stay outside the source folder. Do not commit them.

## Release build

Double-click or run:

```bat
build-release.cmd
```

The release pipeline performs:

1. Visual Studio, CMake, Git, and vcpkg discovery.
2. Source/version/architecture consistency checks.
3. Hardened x64 configuration with warnings treated as errors.
4. Release compilation.
5. The native application target and its link/resource contracts.
6. PE version and security-mitigation verification.
7. Optional certificate-store signing.
8. A distributable ZIP and SHA-256 file.

Outputs are placed under:

```text
%LOCALAPPDATA%\QuickSiftBuild\packages
```

## Useful build commands

Clean and rebuild:

```bat
build.cmd -Clean
```

Build Release without packaging:

```bat
build.cmd -Configuration Release -VerifyBinary
```

Configure the Visual Studio solution (`.slnx` on Visual Studio 2026, `.sln` on Visual Studio 2022) without compiling:

```bat
build.cmd -ConfigureOnly
```

Force Visual Studio 2022 or 2026:

```bat
build.cmd -VisualStudio VisualStudio2022
build.cmd -VisualStudio VisualStudio2026
```

The former CTest suite has been retired. There is no test phase to suppress. Use the source verifier and the manual regression checklist instead.

Prevent automatic vcpkg installation:

```bat
build.cmd -NoBootstrapVcpkg
```

With that switch, set `VCPKG_ROOT` to an existing vcpkg checkout first.

## Signing

The release script signs only when `QS_SIGN_CERT_SHA1` is set:

```bat
set QS_SIGN_CERT_SHA1=YOUR_CERTIFICATE_THUMBPRINT
set QS_TIMESTAMP_URL=https://your-rfc3161-timestamp-service
build-release.cmd -RequireSignature
```

Never commit certificate files, private keys, passwords, tokens, or real signing configuration.

## Visual Studio CMake-folder mode

Visual Studio can also open the source folder directly through **File → Open → Folder**. `CMakePresets.json` provides presets for:

- `windows-vs2026`
- `windows-vs2022`

Folder mode expects `VCPKG_ROOT` to be defined before Visual Studio starts. The generated-solution launcher is easier because it discovers or installs vcpkg automatically.

Preset command examples:

```bat
cmake --preset windows-vs2026
cmake --build --preset vs2026-debug
```

For Visual Studio 2022, replace `2026` with `2022`.

## Portable core checks

The deterministic core builds on non-Windows hosts without the native application or codec dependencies:

```text
cmake -S . -B build-portable -DQS_WARNINGS_AS_ERRORS=ON
cmake --build build-portable
```

This validates portable policy, metadata helpers, cache behavior, and source structure. It does not compile or execute the native Win32 application.

## Direct verification scripts

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-source.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-binary.ps1 -Path <path-to-QuickSift.exe>
```

The authoritative release gate remains a clean native Windows build, source/binary verification, signing verification when required, and the manual matrix in [`PRE_RELEASE_CHECKLIST.md`](PRE_RELEASE_CHECKLIST.md).

### Release hardening compatibility

The default release keeps CFG, ASLR, DEP, CET shadow-stack compatibility, stack protection, and restricted DLL loading. MSVC EH-continuation metadata is intentionally disabled by default because the statically bundled vcpkg libraries are not all compiled with `/guard:ehcont`; enabling it only on QuickSift causes LNK2047/LNK1386. `QS_ENABLE_EHCONT` may be enabled only after every linked static dependency is rebuilt compatibly. Never use `/FORCE:GUARDEHCONT` to bypass this check.

`build-release.cmd` deletes only the generated CMake solution/build directory before configuring. The separate vcpkg installed/download cache is retained.


## Release distribution paths

QuickSift uses one application codebase with runtime package-identity detection.

### Portable ZIP

Run:

```bat
build.bat
```

This uses the hardened Release pipeline and packages the unpackaged executable. At runtime QuickSift writes its own state under `QuickSiftData` beside the executable.

### Microsoft Store MSIX

Reserve the app in Partner Center first, then set the exact identity values and the exact corresponding-source URL for the build being submitted:

```bat
set QS_MSIX_IDENTITY_NAME=<Partner Center Package/Identity Name>
set QS_MSIX_PUBLISHER=<Partner Center Publisher, typically CN=...>
set QS_SOURCE_URL=https://<exact source release URL>
package-msix.bat
```

The script performs a clean verified Release build, stages `QuickSift.exe`, GPL/source notices and MSIX assets, renders `AppxManifest.xml`, and creates `QuickSift-0.9.3.0-RC-x64.msix` with `makeappx.exe`. If `QS_SIGN_CERT_SHA1` is set it can sign for sideload testing. For Microsoft Store submission, the package may be left unsigned because the Store signs/re-signs certified MSIX packages.

When installed as MSIX, QuickSift detects package identity with the native Windows package API and stores QuickSift-owned state under the package `LocalState`; it never attempts to write into the package installation directory. User-requested photo metadata/copy/move/delete operations continue to target the user's selected photo locations.

### Build logs

Every invocation of the root build entry points automatically writes a complete transcript beside the `build*.bat` files. The filename is `QuickSift-BuildLog-HHMMSS.txt`. Native CMake/MSBuild/vcpkg output is streamed live to the console and recorded in full; there is no diagnostic tail truncation.
