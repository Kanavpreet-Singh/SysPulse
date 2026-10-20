# ==============================================================================
# Multi-Stage Dockerfile for SysPulse Telemetry Agent & Analysis Engine
# Stage 1: Build & Test (GCC, CMake, Ninja, Python)
# Stage 2: Minimal Runtime Image with Python Analysis Engine & Daemon
# ==============================================================================

# Stage 1: Build & Test Environment
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    python3 \
    python3-pip \
    python3-venv \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy entire source tree
COPY . .

# Configure and compile with strict warnings
RUN cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build --config Release

# Run C++ Unit & Concurrency Tests inside container
RUN ctest --test-dir build --output-on-failure

# Stage 2: Slim Production Runtime Image
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 \
    python3-pip \
    python3-venv \
    procps \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/syspulse

# Copy compiled binaries from builder stage
COPY --from=builder /app/build/syspulse_agent /opt/syspulse/bin/
COPY --from=builder /app/build/cpu_spin /opt/syspulse/bin/
COPY --from=builder /app/build/mem_leak /opt/syspulse/bin/
COPY --from=builder /app/build/thread_storm /opt/syspulse/bin/
COPY --from=builder /app/build/crash_test /opt/syspulse/bin/

# Copy Python analysis package and scripts
COPY --from=builder /app/analysis /opt/syspulse/analysis
COPY --from=builder /app/scripts /opt/syspulse/scripts
COPY --from=builder /app/kql /opt/syspulse/kql

# Install Python dependencies
ENV VIRTUAL_ENV=/opt/syspulse/venv
RUN python3 -m venv $VIRTUAL_ENV
ENV PATH="$VIRTUAL_ENV/bin:/opt/syspulse/bin:$PATH"
RUN pip install --no-cache-dir pandas numpy matplotlib pytest

# Run Python pytest suite in the runtime environment
ENV PYTHONPATH=/opt/syspulse/analysis
RUN pytest /opt/syspulse/analysis/tests

ENTRYPOINT ["syspulse_agent"]
CMD ["--interval-ms", "500", "--duration-s", "0", "--out", "/var/log/syspulse/samples.ndjson"]
