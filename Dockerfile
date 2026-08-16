# Architecture-neutral on purpose.
#
# The tags below carry no -amd64 suffix, so Docker pulls whichever architecture the
# host actually is. That matters for Oracle Cloud's Always Free tier, where the
# generous shape (4 cores / 24 GB) is ARM: an amd64-pinned image either refuses to
# run there or runs under emulation at a fraction of the speed.
FROM mcr.microsoft.com/dotnet/sdk:8.0-bookworm-slim AS build
WORKDIR /app
ENV XMAKE_ROOT y

RUN echo 'deb http://deb.debian.org/debian bookworm-backports main' >> /etc/apt/sources.list
RUN apt update \
  && apt install -y xmake g++ unzip wget ca-certificates git \
  && apt clean

COPY . .

RUN --mount=type=cache,target=/root/.xmake xmake -y

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
