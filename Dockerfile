FROM mcr.microsoft.com/dotnet/sdk:9.0-bookworm-slim-amd64 AS build
WORKDIR /app
ENV XMAKE_ROOT=y
ENV DOTNET_ROOT=/usr/share/dotnet
ENV PATH=$DOTNET_ROOT:$PATH

RUN echo 'deb http://deb.debian.org/debian bookworm-backports main' >> /etc/apt/sources.list
RUN apt update \
  && apt install -y xmake g++ unzip wget ca-certificates git \
  && wget -q https://dot.net/v1/dotnet-install.sh -O /tmp/dotnet-install.sh \
  && bash /tmp/dotnet-install.sh --channel 8.0 --runtime dotnet --install-dir /usr/share/dotnet \
  && rm /tmp/dotnet-install.sh \
  && apt clean

COPY . .

RUN --mount=type=cache,target=/root/.xmake xmake -y

# RELEASE STAGE
FROM mcr.microsoft.com/dotnet/runtime:8.0.11-bookworm-slim-amd64 AS release
WORKDIR /app

COPY --from=build /app/build/linux/x86_64/release/ /app/


EXPOSE 11778
CMD [ "/app/Server.Loader" ]
