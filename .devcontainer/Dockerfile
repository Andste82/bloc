FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang \
    libclang-rt-dev \
    cmake \
    ninja-build \
    gdb \
    git \
    pkg-config \
    ca-certificates \
    curl \
    ripgrep \
    fd-find \
    shellcheck \
    shfmt \
    xxd \
    valgrind \
    linux-tools-generic \
    gcovr \
    jq \
    doxygen \
    graphviz \
    && rm -rf /var/lib/apt/lists/*

# Bare-metal cross toolchains.
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    gcc-avr \
    binutils-avr \
    avr-libc \
    gcc-riscv64-unknown-elf \
    picolibc-riscv64-unknown-elf \
    && rm -rf /var/lib/apt/lists/*

# Linux cross GCCs and user-mode QEMU: run the unit tests on 32/64-bit,
# little- and big-endian targets (aarch64, armhf, riscv64, powerpc, s390x).
# Ubuntu 26.04 has no qemu-user-static; qemu-user ships static binaries
# (qemu-aarch64, qemu-arm, qemu-ppc, qemu-riscv64, qemu-s390x).
# gcc-multilib is deliberately not installed: it conflicts with every
# Linux cross GCC, so the 32-bit host build (-m32) is CI-only.
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-aarch64-linux-gnu \
    gcc-arm-linux-gnueabihf \
    gcc-riscv64-linux-gnu \
    gcc-powerpc-linux-gnu \
    gcc-s390x-linux-gnu \
    libc6-dev-arm64-cross \
    libc6-dev-armhf-cross \
    libc6-dev-riscv64-cross \
    libc6-dev-powerpc-cross \
    libc6-dev-s390x-cross \
    qemu-user \
    && rm -rf /var/lib/apt/lists/*

# GitHub CLI (gh)
RUN mkdir -p -m 755 /etc/apt/keyrings \
    && curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
        | tee /etc/apt/keyrings/githubcli-archive-keyring.gpg > /dev/null \
    && chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
        > /etc/apt/sources.list.d/github-cli.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends gh \
    && rm -rf /var/lib/apt/lists/*

# Node.js (LTS) für Claude Code
RUN curl -fsSL https://deb.nodesource.com/setup_lts.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

# Claude Code
RUN npm install -g @anthropic-ai/claude-code

WORKDIR /workspace

CMD ["/bin/bash"]
