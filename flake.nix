{
  description = "LCE-Revelations - Minecraft Legacy Console Edition recreation";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    fourjlibs = {
      url = "github:Patoke/4JLibs";
      flake = false;
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      fourjlibs,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };

        # Version derived from network protocol version in cmake/GenerateBuildVer.cmake
        buildNumber = builtins.head (
          builtins.match ".*set\\(BUILD_NUMBER ([0-9]+)\\).*" (
            builtins.readFile ./cmake/GenerateBuildVer.cmake
          )
        );
        version = "0.${buildNumber}.0";

        # Windows SDK downloaded via xwin (fixed-output derivation)
        windowsSdk = pkgs.stdenvNoCC.mkDerivation {
          pname = "windows-sdk-xwin";
          version = "0.6.0";

          outputHashAlgo = "sha256";
          outputHashMode = "recursive";
          outputHash = "sha256-UFQjsFVBwcF/9e9tVFoG0Z1JySxyTnFqoaRwr/tUWzA=";

          nativeBuildInputs = [
            pkgs.xwin
            pkgs.cacert
            pkgs.rsync
          ];

          dontUnpack = true;

          buildPhase = ''
            runHook preBuild

            export HOME=$(mktemp -d)
            TEMP_OUT=$(mktemp -d)

            # Download and splat to temp directory first (same filesystem)
            xwin --accept-license splat --output "$TEMP_OUT"

            # Copy to actual output (handles cross-device)
            mkdir -p $out
            rsync -a "$TEMP_OUT/" "$out/"

            runHook postBuild
          '';

          dontInstall = true;
          dontFixup = true;
        };

        # Pre fetch NuGet packages for FourKit (dotnet publish --self-contained needs win-x64 runtime)
        fourkitNugetDeps = pkgs.stdenvNoCC.mkDerivation {
          pname = "fourkit-nuget-deps";
          version = "10.0";

          outputHashAlgo = "sha256";
          outputHashMode = "recursive";
          outputHash = "sha256-eEkU0MugnFSNvVYvd5V5xLK4oNcLgZcXxMYSuiYMPbA=";

          nativeBuildInputs = [ pkgs.cacert ];

          dontUnpack = true;

          buildPhase = ''
            export HOME=$(mktemp -d)
            export DOTNET_CLI_TELEMETRY_OPTOUT=1
            export DOTNET_NOLOGO=1
            export SSL_CERT_FILE="${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"

            # Use the unwrapped SDK to allow NuGet network access
            DOTNET="${pkgs.dotnetCorePackages.sdk_10_0.passthru.unwrapped}/share/dotnet/dotnet"

            # Copy csproj to writable location (dotnet needs to write obj/)
            WORK=$(mktemp -d)
            cp ${./.}/Minecraft.Server.FourKit/Minecraft.Server.FourKit.csproj "$WORK/"
            cp ${./.}/global.json "$WORK/"

            $DOTNET restore "$WORK/Minecraft.Server.FourKit.csproj" \
              --runtime win-x64 \
              --packages "$out" \
              --source https://api.nuget.org/v3/index.json
          '';

          dontInstall = true;
          dontFixup = true;
        };

        # Helper to make case insensitive symlinks for SDK headers/libs
        sdkWithSymlinks = pkgs.runCommand "windows-sdk-symlinked" { } ''
          cp -r ${windowsSdk} $out
          chmod -R u+w $out

          # SDK header symlinks (case sensitivity fixes)
          ln -sf $out/sdk/include/shared/sdkddkver.h $out/sdk/include/shared/SDKDDKVer.h 2>/dev/null || true

          # Library symlinks (case sensitivity fixes)
          ln -sf $out/sdk/lib/um/x86_64/xinput9_1_0.lib $out/sdk/lib/um/x86_64/XInput9_1_0.lib 2>/dev/null || true
          ln -sf $out/sdk/lib/um/x86_64/ws2_32.lib $out/sdk/lib/um/x86_64/Ws2_32.lib 2>/dev/null || true
        '';

        # The main build derivation
        minecraft-lce-unwrapped = pkgs.stdenv.mkDerivation {
          pname = "minecraft-lce-unwrapped";
          inherit version;

          src = pkgs.lib.cleanSourceWith {
            src = ./.;
            filter =
              path: type:
              let
                baseName = baseNameOf path;
              in
              # Exclude build directories and other non-source files
              !(
                baseName == "build"
                || baseName == "result"
                || baseName == ".git"
                || baseName == ".direnv"
                || pkgs.lib.hasPrefix "result-" baseName
              );
          };

          # Patch in the 4JLibs submodule (flakes don't fetch submodules)
          postUnpack = ''
            rm -rf source/Minecraft.Client/Windows64/4JLibs
            cp -r ${fourjlibs} source/Minecraft.Client/Windows64/4JLibs
            chmod -R u+w source/Minecraft.Client/Windows64/4JLibs
          '';

          nativeBuildInputs = with pkgs; [
            llvmPackages.clang-unwrapped # provides clang-cl
            llvmPackages.lld # provides lld-link
            llvmPackages.llvm # provides llvm-rc, llvm-ml, llvm-lib, llvm-mt
            cmake
            ninja
            rsync
            winePackage # needed to run fxc.exe during build
            dotnetCorePackages.sdk_10_0 # needed for FourKit server
          ];

          configurePhase = ''
            runHook preConfigure

            # Point build-linux.sh at the pre-downloaded Windows SDK
            export XWIN_CACHE=$(mktemp -d)
            ln -s ${sdkWithSymlinks} "$XWIN_CACHE/splat"

            # NuGet packages for FourKit dotnet publish
            export NUGET_PACKAGES="${fourkitNugetDeps}"
            export DOTNET_CLI_TELEMETRY_OPTOUT=1
            export DOTNET_NOLOGO=1

            # Configure build-linux.sh variables
            export SOURCE_DIR=.
            export BUILD_TYPE=Release
            export INSTALL_PREFIX=$out

            # Source the build script for its functions
            source ./build-linux.sh

            BUILD_DIR="$SOURCE_DIR/build/windows64-clang"
            mkdir -p "$BUILD_DIR"

            do_cmake_configure

            # Patch shebangs in generated scripts (fxc wine wrapper)
            patchShebangs "$BUILD_DIR/tools" 2>/dev/null || true

            runHook postConfigure
          '';

          buildPhase = ''
            runHook preBuild
            do_build
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            do_install
            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "Minecraft Legacy Console Edition recreation (Windows executables)";
            homepage = "https://github.com/minecraft-lce/MinecraftConsoles";
            license = licenses.unfree;
            platforms = [ "x86_64-linux" ];
          };
        };

        # Wine package using staging for better performance
        winePackage = pkgs.wineWow64Packages.staging;

        # Wine prefix base path
        winePrefixBase = ".local/share/minecraft-lce";

        # Wrapped client package
        minecraft-lce-client = pkgs.stdenv.mkDerivation {
          pname = "minecraft-lce-client";
          inherit version;

          dontUnpack = true;
          dontBuild = true;

          nativeBuildInputs = [ pkgs.makeWrapper ];

          installPhase = ''
            runHook preInstall

            mkdir -p $out/bin
            mkdir -p $out/share/minecraft-lce-client

            # Copy game files
            cp -r ${minecraft-lce-unwrapped}/client/* $out/share/minecraft-lce-client/

            # Create wrapper script
            cat > $out/bin/minecraft-lce-client << 'WRAPPER'
            #!/usr/bin/env bash
            set -euo pipefail

            GAME_DIR="@gameDir@"
            PERSIST_DIR="''${MC_DATA_DIR:-$HOME/.local/share/minecraft-lce-client}"

            export WINEARCH=win64
            export WINEPREFIX="''${WINEPREFIX:-$HOME/@winePrefixBase@-client}"

            # Wine performance settings
            export WINEDLLOVERRIDES="winemenubuilder.exe=d"
            export WINEESYNC=1
            export WINEFSYNC=1
            export DXVK_LOG_LEVEL=none

            mkdir -p "$PERSIST_DIR"
            mkdir -p "$WINEPREFIX"

            # Create working directory with symlinks to immutable store
            WORK_DIR="$(mktemp -d)"
            trap 'rm -rf "$WORK_DIR"' EXIT

            cp -rs "$GAME_DIR"/* "$WORK_DIR/"
            chmod -R u+w "$WORK_DIR"

            # Setup persistent data directory
            mkdir -p "$PERSIST_DIR/GameHDD"
            rm -rf "$WORK_DIR/Windows64/GameHDD" 2>/dev/null || true
            ln -sf "$PERSIST_DIR/GameHDD" "$WORK_DIR/Windows64/GameHDD"

            cd "$WORK_DIR"

            echo "[info] Starting Minecraft LCE client"
            echo "[info] Data directory: $PERSIST_DIR"
            echo "[info] Wine prefix: $WINEPREFIX"

            exec wine "$WORK_DIR/Minecraft.Client.exe" "$@"
            WRAPPER

            chmod +x $out/bin/minecraft-lce-client

            substituteInPlace $out/bin/minecraft-lce-client \
              --replace "@gameDir@" "$out/share/minecraft-lce-client" \
              --replace "@winePrefixBase@" "${winePrefixBase}"

            # Use wrapProgram to add Wine to PATH (creates proper runtime closure)
            wrapProgram $out/bin/minecraft-lce-client \
              --prefix PATH : "${winePackage}/bin"

            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "Minecraft Legacy Console Edition - Client";
            homepage = "https://github.com/minecraft-lce/MinecraftConsoles";
            license = licenses.unfree;
            platforms = [ "x86_64-linux" ];
            mainProgram = "minecraft-lce-client";
          };
        };

        # Wrapped server package
        minecraft-lce-server = pkgs.stdenv.mkDerivation {
          pname = "minecraft-lce-server";
          inherit version;

          dontUnpack = true;
          dontBuild = true;

          nativeBuildInputs = [ pkgs.makeWrapper ];

          installPhase = ''
            runHook preInstall

            mkdir -p $out/bin
            mkdir -p $out/share/minecraft-lce-server

            # Copy game files
            cp -r ${minecraft-lce-unwrapped}/server/* $out/share/minecraft-lce-server/

            # Create wrapper script
            cat > $out/bin/minecraft-lce-server << 'WRAPPER'
            #!/usr/bin/env bash
            set -euo pipefail

            GAME_DIR="@gameDir@"
            SERVER_PORT="''${MC_PORT:-25565}"
            SERVER_BIND_IP="''${MC_BIND:-0.0.0.0}"
            PERSIST_DIR="''${MC_DATA_DIR:-$HOME/.local/share/minecraft-lce-server}"

            export WINEARCH=win64
            export WINEPREFIX="''${WINEPREFIX:-$HOME/@winePrefixBase@-server}"

            # Wine settings
            export WINEDLLOVERRIDES="winemenubuilder.exe=d"
            export WINEESYNC=1
            export WINEFSYNC=1

            mkdir -p "$PERSIST_DIR"
            mkdir -p "$WINEPREFIX"

            # Create working directory with symlinks to immutable store
            WORK_DIR="$(mktemp -d)"
            trap 'rm -rf "$WORK_DIR"' EXIT

            cp -rs "$GAME_DIR"/* "$WORK_DIR/"
            chmod -R u+w "$WORK_DIR"

            # Setup persistent data
            mkdir -p "$PERSIST_DIR/GameHDD"

            for file in server.properties banned-players.json banned-ips.json; do
              if [[ ! -f "$PERSIST_DIR/$file" ]]; then
                if [[ -f "$WORK_DIR/$file" ]]; then
                  cp "$WORK_DIR/$file" "$PERSIST_DIR/$file"
                else
                  echo "[]" > "$PERSIST_DIR/$file"
                fi
              fi
              ln -sf "$PERSIST_DIR/$file" "$WORK_DIR/$file"
            done

            rm -rf "$WORK_DIR/Windows64/GameHDD" 2>/dev/null || true
            ln -sf "$PERSIST_DIR/GameHDD" "$WORK_DIR/Windows64/GameHDD"

            cd "$WORK_DIR"

            # Start Xvfb if no display (server may require a virtual display)
            if [[ -z "''${DISPLAY:-}" ]]; then
              export DISPLAY=":99"
              Xvfb "$DISPLAY" -nolisten tcp -screen 0 64x64x16 &
              XVFB_PID=$!
              trap 'kill $XVFB_PID 2>/dev/null || true; rm -rf "$WORK_DIR"' EXIT
              sleep 1
              echo "[info] Started Xvfb on $DISPLAY"
            fi

            echo "[info] Starting Minecraft LCE server on $SERVER_BIND_IP:$SERVER_PORT"
            echo "[info] Data directory: $PERSIST_DIR"
            echo "[info] Wine prefix: $WINEPREFIX"

            exec wine "$WORK_DIR/Minecraft.Server.exe" -port "$SERVER_PORT" -bind "$SERVER_BIND_IP" "$@"
            WRAPPER

            chmod +x $out/bin/minecraft-lce-server

            substituteInPlace $out/bin/minecraft-lce-server \
              --replace "@gameDir@" "$out/share/minecraft-lce-server" \
              --replace "@winePrefixBase@" "${winePrefixBase}"

            # Use wrapProgram to add Wine and Xvfb to PATH (creates proper runtime closure)
            wrapProgram $out/bin/minecraft-lce-server \
              --prefix PATH : "${winePackage}/bin:${pkgs.xorg-server}/bin"

            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "Minecraft Legacy Console Edition - Dedicated Server";
            homepage = "https://github.com/minecraft-lce/MinecraftConsoles";
            license = licenses.unfree;
            platforms = [ "x86_64-linux" ];
            mainProgram = "minecraft-lce-server";
          };
        };

        # Build script for development
        buildScript = pkgs.writeShellApplication {
          name = "minecraft-lce-build";
          runtimeInputs = with pkgs; [
            llvmPackages.clang-unwrapped
            llvmPackages.lld
            llvmPackages.llvm
            cmake
            ninja
            xwin
            rsync
            coreutils
            cacert
            winePackage
          ];
          text = ''
            exec bash "${./build-linux.sh}" "$@"
          '';
        };

      in
      {
        packages = {
          # Main packages - build from source
          client = minecraft-lce-client;
          server = minecraft-lce-server;

          # Unwrapped (just the Windows executables)
          unwrapped = minecraft-lce-unwrapped;

          # NuGet deps for FourKit (for debugging)
          fourkit-nuget-deps = fourkitNugetDeps;

          # Windows SDK (for debugging)
          windows-sdk = sdkWithSymlinks;

          default = minecraft-lce-client;
        };

        apps = {
          client = {
            type = "app";
            program = "${minecraft-lce-client}/bin/minecraft-lce-client";
          };
          server = {
            type = "app";
            program = "${minecraft-lce-server}/bin/minecraft-lce-server";
          };
          build = {
            type = "app";
            program = "${buildScript}/bin/minecraft-lce-build";
          };
          default = {
            type = "app";
            program = "${minecraft-lce-client}/bin/minecraft-lce-client";
          };
        };

        devShells.default = pkgs.mkShell {
          name = "minecraft-lce-dev";
          packages = with pkgs; [
            # Cross-compilation toolchain
            llvmPackages.clang-unwrapped
            llvmPackages.lld
            llvmPackages.llvm
            cmake
            ninja
            xwin
            rsync

            # .NET SDK for FourKit server
            dotnetCorePackages.sdk_10_0

            # Wine for testing
            winePackage
            winetricks

            # For running server without display
            xvfb-run

            # Useful tools
            git
            cacert
          ];

          shellHook = ''
            echo "LCE-Revelations development shell"
            echo ""
            echo "Quick build (uses cached SDK):"
            echo "  nix build .#client   # Build client package"
            echo "  nix build .#server   # Build server package"
            echo ""
            echo "Development build (in-tree):"
            echo "  ./build-linux.sh [source_dir] [Release|Debug]"
            echo ""
            echo "Run:"
            echo "  nix run .#client"
            echo "  nix run .#server"
            echo ""
            echo "Environment variables:"
            echo "  XWIN_CACHE  - Windows SDK cache (default: \$PWD/.xwin)"
            echo "  MC_PORT     - Server port (default: 25565)"
            echo "  MC_BIND     - Server bind address (default: 0.0.0.0)"
            echo "  MC_DATA_DIR - Persistent data directory"
            echo "  WINEPREFIX  - Custom Wine prefix"
            echo ""
          '';
        };
      }
    );
}
