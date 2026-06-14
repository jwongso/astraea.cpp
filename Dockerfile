# Multi-stage build for the nz_tenancy binary.
#
# Stage 1 (builder) installs the full toolchain + dev headers, fetches Drogon
# and glaze via CPM, and produces the stripped Release binary.
# Stage 2 (runtime) is a small ubuntu:24.04 with only the .so runtime deps.
#
# Build:    docker build -t astraea-nz-tenancy:latest .
# Run:      docker run --rm -p 8010:8010 \
#               -e LLM_BASE_URL=http://host.docker.internal:8080/v1 \
#               -e QDRANT_URL=http://host.docker.internal:6333 \
#               astraea-nz-tenancy:latest
#
# Most users will go through docker-compose.yml instead.
#
# Notes:
#   - We do NOT use the dev preset (ASan/UBSan) - the runtime image is
#     Release. The dev preset is for local hacking + CI.
#   - ASTRAEA_BUILD_BINDINGS=OFF: the runtime image doesn't need the
#     pybind11 differential parity hook; that's a build-time tool only.
#   - ASTRAEA_NATIVE_OPTS=OFF: avoid baking in -march=znver5; the same
#     image runs on Intel + AMD + Apple Silicon (under Rosetta) hosts.
#   - mimalloc_override.cpp is compiled into the binary, so the runtime
#     image does NOT need LD_PRELOAD - the override is built-in. We still
#     install libmimalloc2.0 for any future tooling that links it.

ARG UBUNTU_TAG=24.04

# ---------------------------------------------------------------------------
# Stage 1: builder
# ---------------------------------------------------------------------------
FROM ubuntu:${UBUNTU_TAG} AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -q && apt-get install -y --no-install-recommends \
        ca-certificates \
        clang \
        cmake \
        ninja-build \
        git \
        curl \
        libspdlog-dev \
        libre2-dev \
        libhiredis-dev \
        libmimalloc-dev \
        libssl-dev \
        libjsoncpp-dev \
        uuid-dev \
        zlib1g-dev \
        libbrotli-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DASTRAEA_BUILD_TESTS=OFF \
        -DASTRAEA_BUILD_BINDINGS=OFF \
        -DASTRAEA_BUILD_APPS=ON \
        -DASTRAEA_BUILD_CLIENTS=ON \
        -DASTRAEA_NATIVE_OPTS=OFF \
    && cmake --build build --target nz_tenancy -j"$(nproc)" \
    && strip build/apps/nz_tenancy/nz_tenancy

# ---------------------------------------------------------------------------
# Stage 2: runtime
# ---------------------------------------------------------------------------
FROM ubuntu:${UBUNTU_TAG} AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Runtime libraries only. Using the -dev packages here is mildly wasteful
# (~10 MB larger image) but it sidesteps Ubuntu's per-version runtime
# package naming (libhiredis1.1.0 vs libhiredis0.14 etc.) so the same
# Dockerfile works against future Ubuntu LTS without surgery. The -dev
# packages depend on the runtime libs, so we get everything we need.
RUN apt-get update -q && apt-get install -y --no-install-recommends \
        ca-certificates \
        wget \
        libspdlog-dev \
        libre2-dev \
        libhiredis-dev \
        libmimalloc-dev \
        libssl-dev \
        libjsoncpp-dev \
        uuid-dev \
        zlib1g-dev \
        libbrotli-dev \
    && rm -rf /var/lib/apt/lists/*

# Non-root user. Container processes write only to /var/lib/astraea (volume
# mount point for feedback/route_debug JSONLs); the binary itself is owned
# by root and read-execute by everyone.
RUN useradd --system --uid 10001 --no-create-home --shell /usr/sbin/nologin astraea \
    && mkdir -p /var/lib/astraea \
    && chown astraea:astraea /var/lib/astraea

COPY --from=builder /src/build/apps/nz_tenancy/nz_tenancy /usr/local/bin/nz_tenancy

USER astraea
WORKDIR /var/lib/astraea

# Default port; override with $PORT.
EXPOSE 8010

# /health is the cheap liveness probe (no upstream calls). /healthz is the
# deep readiness probe (pings qdrant + llm + embed + rerank); use that one
# for orchestrator readinessProbe + load balancer health checks.
HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
    CMD ["sh", "-c", "wget -q -O /dev/null http://localhost:${PORT:-8010}/health || exit 1"]

ENTRYPOINT ["/usr/local/bin/nz_tenancy"]
