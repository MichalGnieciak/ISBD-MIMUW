# Image is created from 2 stages:
# 1. Build the binaries
# 2. Copy them into the image to keep it fast and minimal

# Using Ubuntu 24.04 to have dependencies available under apt
# build-essential & cmake: for C++
# libzstd-dev: ZSTD 
# nlohmann-json3-dev: JSON library for the Metastore and REST payloads.
# libcpp-httplib-dev: C++ HTTP server library (supports reading JSON bodies and multi-threaded request handling).

# Stage 1
# Building
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libzstd-dev \
    nlohmann-json3-dev \
    libcpp-httplib-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copying required files
COPY projects/proj2 /app/projects/proj2
COPY projects/proj3 /app/projects/proj3

# Building the server binary
RUN cmake -S /app/projects/proj3 -B /app/build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build /app/build -j$(nproc)


# Stage 2
# Runtime
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
# Dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libzstd1 \
    libcpp-httplib-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Binaries
COPY --from=builder /app/build/isbd_server /app/isbd_server

# Creating default mount point for external CSVs and persistent DB storage
RUN mkdir -p /data/db

# Exposing the REST API port
EXPOSE 8080

# Starting the database server pointing to /data/db
ENTRYPOINT ["/app/isbd_server"]
# Default data directory is /data/db and port is 8080
# Specyfing data-dir for local testing without docker
CMD ["--data-dir", "/data/db", "--port", "8080"]