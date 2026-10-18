# Compile Instructions

## Prerequisites

- **Visual Studio 2022** with the **Desktop development with C++** workload (this includes the CMake tools, MSVC toolchain, and Windows 10 SDK).
- **.NET 10 SDK**, required to build the FourKit plugin host (`Minecraft.Server.FourKit`).
  - Download: https://dotnet.microsoft.com/download/dotnet/10.0 (pick the **x64 SDK** installer)
  - The exact SDK version is pinned in `global.json` at the repo root.
  - CMake will fail configure with a clear error message if .NET 10 is not installed, so you find out immediately rather than partway through a build.
  - The build invokes `dotnet publish ... --runtime win-x64 --self-contained true`, so the published output bundles a complete .NET 10 runtime alongside the FourKit assembly. End users running the produced server do **not** need to install .NET themselves.
  - All FourKit runtime files (DLL + .NET runtime + `hostfxr.dll`) land in a `runtime/` subfolder next to `Minecraft.Server.exe`. An empty `plugins/` folder is also created. Both are produced automatically by the build.

## Visual Studio 2022 quick start (recommended)

VS 2022 has built-in CMake support, so there is no need to generate a `.sln` file by hand.

1. Install the prerequisites above.
2. Clone the repo.
3. In Visual Studio: `File > Open > Folder...` and select the **repo root** (the folder that contains `CMakeLists.txt`).
4. Wait for CMake to configure (~5 seconds on a warm cache, a few minutes on the first run while assets copy).
5. Pick a build configuration in the dropdown, for example `windows64-release`.
6. `Build > Build All` (or `F7`). Targets of interest:
   - `Minecraft.Client`: the game client.
   - `Minecraft.Server`: the **vanilla** dedicated server. Standalone C++ binary, no plugin host, no .NET dependency at runtime, smallest distribution.
   - `Minecraft.Server.FourKit`: the **FourKit-enabled** dedicated server. Bundles the .NET 10 plugin host alongside the exe (in `runtime/`) and creates an empty `plugins/` folder for end users to drop plugin DLLs into. Building this target also triggers the `Minecraft.Server.FourKit.Managed` target which publishes the C# project.
7. Use the debug target dropdown to pick `Minecraft.Client.exe` or whichever server flavour you want, then `F5` to launch.

### Server flavours

Both server targets compile from the same source tree and produce a binary literally named `Minecraft.Server.exe`. The variant identity lives in the build directory:

```
build/<preset>/Minecraft.Server/Release/
  Minecraft.Server.exe          (vanilla, no plugin support)
  Common/, Windows64/, ...

build/<preset>/Minecraft.Server.FourKit/Release/
  Minecraft.Server.exe          (FourKit-enabled, same exe name on purpose)
  runtime/                      (self-contained .NET 10 + Minecraft.Server.FourKit.dll)
  plugins/                      (empty drop point)
  Common/, Windows64/, ...
```

The FourKit target gets the `MINECRAFT_SERVER_FOURKIT_BUILD` preprocessor define. Inside `FourKitBridge.h`, the real plugin entry points are conditional on that define; the vanilla target sees inline no-op stubs instead, so gameplay code can call `FourKitBridge::Fire*` unconditionally and produce the right behaviour for each flavour without per-call-site `#ifdef`s.

### Dedicated server debug arguments

- Default debugger arguments for both `Minecraft.Server` and `Minecraft.Server.FourKit`:
  - `-port 25565 -bind 0.0.0.0 -name DedicatedServer`
- You can override arguments in:
  - `Project Properties > Debugging > Command Arguments`
- Both server targets post-build copy the dedicated-server asset set:
  - `Common/Media/MediaWindows64.arc`
  - `Common/res`
  - `Windows64/GameHDD`

## CMake (Windows x64)

Configure (use your VS Community instance explicitly):

Open `Developer PowerShell for VS` and run:

```powershell
cmake --preset windows64
```

Build Debug:

```powershell
cmake --build --preset windows64-debug --target Minecraft.Client
```

Build Release:

```powershell
cmake --build --preset windows64-release --target Minecraft.Client
```

Build vanilla Dedicated Server (Debug):

```powershell
cmake --build --preset windows64-debug --target Minecraft.Server
```

Build vanilla Dedicated Server (Release):

```powershell
cmake --build --preset windows64-release --target Minecraft.Server
```

Build FourKit Dedicated Server (Debug):

```powershell
cmake --build --preset windows64-debug --target Minecraft.Server.FourKit
```

Build FourKit Dedicated Server (Release):

```powershell
cmake --build --preset windows64-release --target Minecraft.Server.FourKit
```

Build everything (client + both server flavours):

```powershell
cmake --build --preset windows64-release
```

Run executable:

```powershell
cd .\build\windows64\Minecraft.Client\Debug
.\Minecraft.Client.exe
```

Run vanilla dedicated server:

```powershell
cd .\build\windows64\Minecraft.Server\Debug
.\Minecraft.Server.exe -port 25565 -bind 0.0.0.0 -name DedicatedServer
```

Run FourKit dedicated server:

```powershell
cd .\build\windows64\Minecraft.Server.FourKit\Debug
.\Minecraft.Server.exe -port 25565 -bind 0.0.0.0 -name DedicatedServer
```

Notes:
- Post-build asset copy is automatic for `Minecraft.Client` in CMake (Debug and Release variants).
- The game relies on relative paths (for example `Common\Media\...`), so launching from the output directory is required.

## CMake (Linux x64 Cross-Compile with Clang)

Cross-compile Windows x64 binaries on Linux using LLVM/Clang and the Windows SDK obtained via xwin.

### Prerequisites

Install the following packages (example for Ubuntu):

```bash
sudo apt install clang lld llvm cmake ninja-build rsync cargo
```

Install xwin for downloading the Windows SDK:

```bash
cargo install xwin
```

### Compile

Run this (Release):
```bash
./build-linux.sh
```

Or, for debug:
```bash
./build-linux.sh . Debug
```


### NixOS / Nix

For NixOS or systems with Nix installed, use the provided flake:

```bash
nix build .#client
nix build .#server
```

Or enter the development shell with all dependencies:

```bash
nix develop
```

Notes:
- Requires LLVM 15+ with clang-cl, lld-link, llvm-rc, and llvm-mt.
- Wine is required to run the compiled Windows executables on Linux.

### Troubleshooting

- **`'vswhere.exe' is not recognized`**: harmless warning. This appears if you ran `vcvars64.bat` from a plain command prompt instead of `Developer PowerShell for VS`. The Visual Studio Installer's `vswhere.exe` lives at `C:\Program Files (x86)\Microsoft Visual Studio\Installer\` and is not on the default `PATH`. Use the Developer PowerShell shortcut, or open the repo folder directly in VS (which handles the dev env for you).
- **`.NET 10 SDK not found` at configure time**: install the x64 SDK from https://dotnet.microsoft.com/download/dotnet/10.0 and re-run CMake configure (`Project > Configure Cache` in VS, or `cmake --preset windows64` from a shell).
- **Server starts but logs `hostfxr_initialize_for_dotnet_command_line failed`**: the `runtime/` folder next to `Minecraft.Server.exe` is missing or stale. Rebuild the `Minecraft.Server.FourKit` target (which re-stages `runtime/`), or do a clean rebuild of `Minecraft.Server`.
