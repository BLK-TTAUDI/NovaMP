FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y \
    git cmake ninja-build pkg-config \
    g++ curl zip unzip tar \
    libssl-dev libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/microsoft/vcpkg /vcpkg && \
    /vcpkg/bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT=/vcpkg

WORKDIR /src
COPY . .

RUN cmake -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target novaMP-master

FROM ubuntu:22.04
RUN apt-get update && apt-get install -y \
    libssl3 libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/build/server/novaMP-master .
COPY server/config.toml .
COPY server/schema.sql .

RUN mkdir -p data
VOLUME ["/app/data"]

EXPOSE 8080
CMD ["./novaMP-master", "config.toml"]
