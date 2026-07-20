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
    meson \
    ninja-build \
    pkg-config \
    libi2c-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt

RUN curl -L \
    "https://github.com/koreader/koxtoolchain/releases/download/${KOXTOOLCHAIN_VERSION}/${KOXTOOLCHAIN_ARCHIVE}" \
    -o /tmp/kobo.tar.gz \
    && tar -xzf /tmp/kobo.tar.gz -C /opt \
    && rm -f /tmp/kobo.tar.gz

WORKDIR /work