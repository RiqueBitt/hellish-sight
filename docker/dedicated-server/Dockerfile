FROM debian:bookworm-slim

ARG DEBIAN_FRONTEND=noninteractive
# Build the server with CMake first (see COMPILE.md). The build context is
# the repo root, and the path below points at the **vanilla** server's Release
# output. The vanilla flavour is shipped because it has no .NET dependency
# and runs reliably under Wine.
#
# A FourKit-enabled image is intentionally NOT built here yet: hosting the
# .NET 10 self-contained runtime through Wine has not been smoke-tested.
# Once it's validated, this Dockerfile can be extended (or split into a
# second variant) to bake in the runtime/ and plugins/ folders from
# build/windows64/Minecraft.Server.FourKit/Release.
ARG MC_RUNTIME_DIR=build/windows64/Minecraft.Server/Release

RUN dpkg --add-architecture i386 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
    ca-certificates \
    wine \
    wine64 \
    wine32:i386 \
    xvfb \
    tini \
    && rm -rf /var/lib/apt/lists/*

ENV WINEARCH=win64
ENV WINEPREFIX=/var/opt/wineprefix64

WORKDIR /srv/mc

COPY ${MC_RUNTIME_DIR}/Minecraft.Server.exe /srv/mc/Minecraft.Server.exe
COPY ${MC_RUNTIME_DIR}/iggy_w64.dll /srv/mc/iggy_w64.dll
COPY ${MC_RUNTIME_DIR}/Common /srv/mc/Common
COPY ${MC_RUNTIME_DIR}/Windows64 /srv/mc/Windows64
COPY docker/dedicated-server/entrypoint.sh /usr/local/bin/entrypoint.sh

RUN chmod 0755 /usr/local/bin/entrypoint.sh \
    && mkdir -p /var/opt/wineprefix64 /srv/mc \
    && test -f /srv/mc/Minecraft.Server.exe

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/entrypoint.sh"]
