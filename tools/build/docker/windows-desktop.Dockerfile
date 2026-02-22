FROM fedora:41

ENV FIREBIRD_QT_VERSION=6.10.1

RUN dnf -y install \
    cmake \
    ninja-build \
    make \
    git \
    python3 \
    python3-pip \
    mingw64-gcc-c++ \
    mingw64-winpthreads-static \
    zlib-devel \
    mingw64-zlib \
    && dnf clean all

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
    aqt install-qt windows desktop "${FIREBIRD_QT_VERSION}" win64_mingw --outputdir /opt/qt; \
    if [ -d "/opt/qt/${FIREBIRD_QT_VERSION}/mingw_64" ]; then \
      ln -s "/opt/qt/${FIREBIRD_QT_VERSION}/mingw_64" /opt/qt/windows; \
    elif [ -d "/opt/qt/${FIREBIRD_QT_VERSION}/win64_mingw" ]; then \
      ln -s "/opt/qt/${FIREBIRD_QT_VERSION}/win64_mingw" /opt/qt/windows; \
    else \
      echo "Qt Windows MinGW kit not found after install"; \
      exit 1; \
    fi; \
    ln -s "/opt/qt/${FIREBIRD_QT_VERSION}/${qt_arch}" /opt/qt/host

ENV FIREBIRD_QT_WINDOWS_ROOT=/opt/qt/windows
ENV FIREBIRD_QT_HOST_ROOT=/opt/qt/host
ENV PATH="/opt/qt/host/bin:${PATH}"

COPY tools/build/docker/mingw64-toolchain.cmake /opt/firebird/toolchains/mingw64-toolchain.cmake

WORKDIR /src
