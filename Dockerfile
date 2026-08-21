# Architecture-neutral on purpose.
#
# The tags below carry no -amd64 suffix, so Docker pulls whichever architecture the
# host actually is. Cheap cloud ARM hosts are common, and an amd64-pinned image
# either refuses to run on them or runs under emulation at a fraction of the speed.
# SDK 9, runtime 8: SdkGenerator (build-time codegen) targets net9.0, everything that
# actually ships - Server.Loader and every plugin - targets net8.0. The SDK builds both;
# the release stage keeps the exact runtime the shipped binaries ask for.
FROM mcr.microsoft.com/dotnet/sdk:9.0-bookworm-slim AS build
WORKDIR /app
ENV XMAKE_ROOT y

# pnpm imports packages from its store with copy_file_range, which ZFS-backed Docker
# storage can fail with a spurious EAGAIN mid-install (OpenZFS + overlayfs interaction;
# TrueNAS hosts are exactly that stack). Hardlinking from the store avoids the syscall
# entirely - same filesystem inside the build stage, and the store is discarded with
# the stage anyway.
ENV npm_config_package_import_method hardlink

RUN echo 'deb http://deb.debian.org/debian bookworm-backports main' >> /etc/apt/sources.list
# xmake-data Recommends cmake, and apt installs recommends by default - which plants
# Debian's cmake 3.25 in the image. With a system cmake present, xmake uses it instead
# of fetching its own, and entt v4.0.0 demands cmake >= 3.28. Removing it (a Recommends,
# so xmake itself survives) puts xmake back on its fetch-a-modern-one path.
# nodejs/npm: EmoteSystem's csproj builds its Admin web app with pnpm during publish.
# pnpm pinned to 9 to match the toolchain pin used everywhere else on this project.
RUN apt update \
  && apt install -y xmake g++ unzip wget ca-certificates git nodejs npm \
  && apt remove -y cmake cmake-data \
  && npm install --global pnpm@9 \
  && apt clean

COPY . .

# Capped parallelism, for the same reason CONTRIBUTING caps it on Windows: this build
# has exhausted memory before, and here the deaths were silent - dependency compiles
# (cryptopp, abseil, protobuf) just stopped mid-file with no compiler error, killed
# under memory pressure on a 4-core/ZFS host. Two jobs builds everywhere we have
# tried; raise it per-host with --build-arg BUILD_JOBS=N if the hardware has headroom.
ARG BUILD_JOBS=2

# Retried: xmake downloads package sources here, and the mirrors 504 under load -
# a Linux CI run died on cryptopp/flecs/entt fetches with no code change. A retry
# resumes from the package cache, so a second attempt only refetches what failed.
RUN --mount=type=cache,target=/root/.xmake sh -c 'for i in 1 2 3; do xmake -y -j${BUILD_JOBS} && exit 0; echo "xmake build failed (attempt $i) - retrying in 20s"; sleep 20; done; exit 1'

# xmake writes to build/linux/<arch>/release, where <arch> is x86_64 on Intel and
# arm64 on ARM. Collect it into a fixed path so the release stage does not have to
# know which one it was - hardcoding x86_64 here is what made this image amd64-only.
RUN mkdir -p /out && cp -r /app/build/linux/*/release/. /out/

# RELEASE STAGE
FROM mcr.microsoft.com/dotnet/runtime:8.0-bookworm-slim AS release
WORKDIR /app

COPY --from=build /out/ /app/

# UDP is the game traffic; the same port serves the admin/web API over TCP.
EXPOSE 11778/udp
EXPOSE 11778/tcp

CMD [ "/app/Server.Loader" ]
