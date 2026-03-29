FROM ubuntu:22.04 AS builder
RUN apt-get update && apt-get install -y \
    git cmake ninja-build pkg-config g++ curl zip unzip tar \
    libssl-dev zlib1g-dev liblua5.3-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/microsoft/vcpkg /vcpkg && \
    /vcpkg/bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT=/vcpkg

WORKDIR /src
COPY . .
RUN cmake -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target novaMP-server

FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libssl3 zlib1g liblua5.3-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/build/servers/novaMP-server .
COPY --from=builder /src/build/servers/Resources ./Resources
COPY servers/ServerConfig.toml .

VOLUME ["/app/Resources/Server/plugins"]
EXPOSE 4444/udp 4444/tcp 4445/tcp
CMD ["./novaMP-server", "ServerConfig.toml"]
