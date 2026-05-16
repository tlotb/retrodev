FROM mcr.microsoft.com/devcontainers/cpp:2-debian13

ARG REINSTALL_CMAKE_VERSION_FROM_SOURCE="none"

# Optionally install the cmake for vcpkg
COPY ./reinstall-cmake.sh /tmp/

RUN if [ "${REINSTALL_CMAKE_VERSION_FROM_SOURCE}" != "none" ]; then \
        chmod +x /tmp/reinstall-cmake.sh && /tmp/reinstall-cmake.sh ${REINSTALL_CMAKE_VERSION_FROM_SOURCE}; \
    fi \
    && rm -f /tmp/reinstall-cmake.sh

# [Optional] Uncomment this section to install additional vcpkg ports.
# RUN su vscode -c "${VCPKG_ROOT}/vcpkg install <your-port-name-here>"

# [Optional] Uncomment this section to install additional packages.
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends libfreetype-dev libsdl3-dev libsdl3-image-dev lld \
    && rm -rf /var/lib/apt/lists/*

# Install Kombine build runner (mkb)
RUN ARCH="$(dpkg --print-architecture)" \
    && case "$ARCH" in \
        amd64) ;; \
        *) echo "Unsupported architecture for Kombine binary: $ARCH"; exit 1 ;; \
    esac \
    && curl -fsSL -o /tmp/kombine.lnx.tar.gz https://github.com/kollective-networks/kltv.kombine/releases/latest/download/kombine.lnx.tar.gz \
    && tar -xzf /tmp/kombine.lnx.tar.gz -C /tmp \
    && install -m 0755 /tmp/mkb /usr/local/bin/mkb \
    && rm -f /tmp/kombine.lnx.tar.gz /tmp/mkb
