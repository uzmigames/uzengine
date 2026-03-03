# =============================================================================
# UzEngine Dev Container
# =============================================================================
# Base: Ubuntu 24.04 LTS (noble) — latest LTS with good Clang 18 support
# What's installed:
#   - Clang 18 + clangd 18 (C/C++ compiler and language server)
#   - CMake 3.28+ (from Kitware APT)
#   - Ninja (fast build)
#   - vcpkg (Microsoft C++ package manager)
#   - Node 22 LTS + npm (TypeScript / VSCode extension build)
#   - Git, curl, wget, unzip, pkg-config (build utilities)
#   - Python3 (required by some vcpkg ports)
#   - GTest built via vcpkg (not system package — matches vcpkg triplet)
#
# NOTE: DX12/DXGI are Windows-only. The container is used for:
#   - C++ unit tests (Core Layer, RHI common — no GPU required)
#   - TypeScript tests (Jest)
#   - Code editing, linting, type-checking
#   - CI pipeline
# =============================================================================

FROM ubuntu:24.04

# Avoid interactive prompts during package install
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# ---------------------------------------------------------------------------
# System packages
# ---------------------------------------------------------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Build essentials
    build-essential \
    curl \
    wget \
    git \
    unzip \
    zip \
    pkg-config \
    ninja-build \
    # Clang 18
    clang-18 \
    clangd-18 \
    lld-18 \
    llvm-18 \
    libc++-18-dev \
    libc++abi-18-dev \
    # Python (vcpkg ports)
    python3 \
    python3-pip \
    # Required by some vcpkg ports
    libssl-dev \
    libffi-dev \
    # Useful CLI tools
    jq \
    ripgrep \
    tree \
    ca-certificates \
    gnupg \
    lsb-release \
    sudo \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# Symlink clang-18 as default clang/clang++/clangd
# ---------------------------------------------------------------------------
RUN update-alternatives --install /usr/bin/clang   clang   /usr/bin/clang-18   100 \
 && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100 \
 && update-alternatives --install /usr/bin/clangd  clangd  /usr/bin/clangd-18  100 \
 && update-alternatives --install /usr/bin/lld     lld     /usr/bin/lld-18     100

# ---------------------------------------------------------------------------
# CMake 3.28+ from Kitware APT (Ubuntu 24.04 has 3.28 in universe, but
# Kitware always has the latest stable release)
# ---------------------------------------------------------------------------
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null \
    | gpg --dearmor - \
    | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null \
 && echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] \
    https://apt.kitware.com/ubuntu/ noble main" \
    > /etc/apt/sources.list.d/kitware.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends cmake \
 && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# Node 22 LTS via NodeSource
# ---------------------------------------------------------------------------
RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
 && apt-get install -y --no-install-recommends nodejs \
 && rm -rf /var/lib/apt/lists/* \
 && node --version && npm --version

# ---------------------------------------------------------------------------
# vcpkg (Microsoft C++ package manager)
# Pinned to a recent stable commit for reproducibility.
# ---------------------------------------------------------------------------
ENV VCPKG_ROOT=/opt/vcpkg
ENV VCPKG_DEFAULT_TRIPLET=x64-linux
ENV VCPKG_DISABLE_METRICS=1

RUN git clone https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} \
 && ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics \
 && ln -s ${VCPKG_ROOT}/vcpkg /usr/local/bin/vcpkg

# ---------------------------------------------------------------------------
# Non-root user 'vscode' (matches Dev Container convention)
# ---------------------------------------------------------------------------
RUN groupadd --gid 1000 vscode \
 && useradd  --uid 1000 --gid vscode --shell /bin/bash --create-home vscode \
 && echo "vscode ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers.d/vscode \
 && chmod 0440 /etc/sudoers.d/vscode \
 # Give vscode ownership of vcpkg so it can install packages
 && chown -R vscode:vscode ${VCPKG_ROOT}

# ---------------------------------------------------------------------------
# Claude Code CLI
# (installed globally so the 'vscode' user can run `claude` from anywhere)
# ---------------------------------------------------------------------------
RUN npm install -g @anthropic-ai/claude-code

# ---------------------------------------------------------------------------
# Compiler and CMake toolchain defaults for the vscode user
# ---------------------------------------------------------------------------
USER vscode
ENV CC=clang
ENV CXX=clang++
ENV CMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake

WORKDIR /workspaces/UzEngine
