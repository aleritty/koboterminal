FROM debian:bookworm-slim

ARG KOXTOOLCHAIN_VERSION=2025.05
ARG KOXTOOLCHAIN_ARCHIVE=kobo.tar.gz

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    ca-certificates \
    curl \
    git \
    xz-utils \
    build-essential \
    ninja-build \
    pkg-config \
    qtbase5-dev \
    qttools5-dev-tools \
    libi2c-dev \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# Upgrade meson to a recent version (1.11+) for subproject support
RUN pip3 install meson --break-system-packages

WORKDIR /opt

RUN curl -L \
    "https://github.com/koreader/koxtoolchain/releases/download/${KOXTOOLCHAIN_VERSION}/${KOXTOOLCHAIN_ARCHIVE}" \
    -o /tmp/kobo.tar.gz \
    && tar -xzf /tmp/kobo.tar.gz -C /opt \
    && rm -f /tmp/kobo.tar.gz

WORKDIR /work