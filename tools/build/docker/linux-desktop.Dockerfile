FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV FIREBIRD_QT_VERSION=6.10.1

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ninja-build \
    pkg-config \
    python3 \
    python3-pip \
    ca-certificates \
    libgl1-mesa-dev \
    libegl1-mesa-dev \
    libxkbcommon-x11-0 \
    libxkbcommon-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --no-cache-dir aqtinstall

RUN set -eux; \
    arch="$(uname -m)"; \
    if [ "${arch}" = "x86_64" ]; then \
      qt_host="linux"; \
      qt_arch="gcc_64"; \
    elif [ "${arch}" = "aarch64" ]; then \
      qt_host="linux_arm64"; \
      qt_arch="linux_gcc_arm64"; \
    else \
      echo "Unsupported architecture: ${arch}"; \
      exit 1; \
    fi; \
    aqt install-qt "${qt_host}" desktop "${FIREBIRD_QT_VERSION}" "${qt_arch}" --outputdir /opt/qt; \
    ln -s "/opt/qt/${FIREBIRD_QT_VERSION}/${qt_arch}" /opt/qt/host

ENV PATH="/opt/qt/host/bin:${PATH}"
ENV CMAKE_PREFIX_PATH="/opt/qt/host"

WORKDIR /src
