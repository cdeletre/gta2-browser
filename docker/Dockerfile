# Image de build du port natif GTA2 (aarch64), basée sur l'image officielle PortMaster.
#   docker build --platform=linux/arm64 -t gta2-pm-img docker/
FROM ghcr.io/monkeyx-net/portmaster-build-templates/portmaster-builder:aarch64-latest

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        clang lld llvm \
        cmake ninja-build \
        git python3 patch \
        file binutils \
        rsync openssh-client \
        nodejs \
    && rm -rf /var/lib/apt/lists/*

RUN git config --global --add safe.directory '*'

WORKDIR /workspace
ENTRYPOINT []
CMD ["sleep", "infinity"]
