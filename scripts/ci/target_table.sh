#!/usr/bin/env bash
# shellcheck shell=bash disable=SC2034
# Single source of truth for every build target (implementation plan, sections 3.6 and 10.4).
#
# Source this file, then call `bloc_target <name>`. It sets:
#   BLOC_KIND               host | emulated | baremetal
#   BLOC_CCID               gcc | clang (which warning flags apply)
#   BLOC_CC                 compiler command
#   BLOC_TARGET_FLAGS       extra compiler flags of the target (space separated, no quoting)
#   BLOC_ISYSTEM            C library include directory for Clang cross builds (or empty)
#   BLOC_TOOLCHAIN_FILE     CMake toolchain file (emulated targets), relative to the repository
#   BLOC_NM, BLOC_SIZE      binutils for the target
#   BLOC_EMULATOR           user-mode emulator (emulated targets)
#   BLOC_CONFIGS            test configurations that apply to the target
#   BLOC_EXPECT_BIG_ENDIAN, BLOC_EXPECT_PTR_BITS   expectations of test_platform (EM-04), or empty
#   BLOC_REQUIRED_TOOLS     commands that must exist, otherwise the target is skipped
#   BLOC_UNAVAILABLE        non-empty: reason why the target cannot run on this machine
# `bloc_family <name>` lists the bare-metal targets of a family, `bloc_families` lists families.
# `bloc_expected_failure <target> <config>` prints the compile error a combination must produce.
#
# This file does not call `set -e`; it is meant to be sourced.

# Strict mode (CI, or BLOC_STRICT=1): every tool is required, so a missing tool or an unavailable
# target is a failure instead of a SKIP (plan 9.4, 10.4).
bloc_strict() {
    [ "${BLOC_STRICT:-0}" = 1 ] || [ "${CI:-}" = true ] || [ "${GITHUB_ACTIONS:-}" = true ]
}

BLOC_ALL_CONFIGS="default debug nochecks wide noalign bigalign pthread"
BLOC_GATED_CONFIGS="default debug nochecks wide noalign bigalign"
BLOC_EMULATED_TARGETS="aarch64 armhf riscv64 powerpc s390x"
BLOC_BAREMETAL_FAMILIES="arm-gcc clang riscv-gcc avr-gcc"

bloc_families() {
    echo "$BLOC_BAREMETAL_FAMILIES"
}

bloc_family() {
    case "$1" in
    arm-gcc) echo "cm0plus-gcc cm3-gcc cm4-gcc ca7-gcc cr5-gcc" ;;
    clang) echo "cm0plus-clang cm3-clang rv32imac-clang" ;;
    riscv-gcc) echo "rv32imac-gcc rv32i-gcc rv64imac-gcc" ;;
    avr-gcc) echo "atmega328p-gcc" ;;
    *)
        echo "unknown family: $1" >&2
        return 1
        ;;
    esac
}

bloc_all_baremetal() {
    local f
    for f in $BLOC_BAREMETAL_FAMILIES; do
        bloc_family "$f"
    done
}

bloc_expected_failure() {
    case "$1:$2" in
    atmega328p-gcc:wide) echo "BLOC_SIZE_T must not be wider than size_t" ;;
    *) echo "" ;;
    esac
}

_bloc_ccid_of() {
    if "$1" --version 2>/dev/null | grep -qi clang; then echo clang; else echo gcc; fi
}

_bloc_reset() {
    BLOC_KIND=""
    BLOC_CCID="gcc"
    BLOC_CC=""
    BLOC_TARGET_FLAGS=""
    BLOC_ISYSTEM=""
    BLOC_TOOLCHAIN_FILE=""
    BLOC_NM="nm"
    BLOC_SIZE="size"
    BLOC_EMULATOR=""
    BLOC_CONFIGS="$BLOC_ALL_CONFIGS"
    BLOC_EXPECT_BIG_ENDIAN=""
    BLOC_EXPECT_PTR_BITS=""
    BLOC_REQUIRED_TOOLS=""
    BLOC_UNAVAILABLE=""
}

_bloc_emulated() { # name triple emulator big_endian ptr_bits
    BLOC_KIND=emulated
    BLOC_CC="$2-gcc"
    BLOC_TOOLCHAIN_FILE="cmake/toolchains/linux-$1.cmake"
    BLOC_NM="$2-nm"
    BLOC_SIZE="$2-size"
    BLOC_EMULATOR="$3"
    BLOC_EXPECT_BIG_ENDIAN="$4"
    BLOC_EXPECT_PTR_BITS="$5"
    BLOC_REQUIRED_TOOLS="$BLOC_CC $3 cmake ninja"
}

_bloc_baremetal() { # ccid cc nm size flags
    BLOC_KIND=baremetal
    BLOC_CCID="$1"
    BLOC_CC="$2"
    BLOC_NM="$3"
    BLOC_SIZE="$4"
    BLOC_TARGET_FLAGS="$5"
    BLOC_CONFIGS="$BLOC_GATED_CONFIGS"
    BLOC_REQUIRED_TOOLS="$2 $3"
}

_bloc_newlib=/usr/lib/arm-none-eabi/include
_bloc_picolibc=/usr/lib/picolibc/riscv64-unknown-elf/include

bloc_target() {
    _bloc_reset
    case "$1" in
    host)
        BLOC_KIND=host
        BLOC_CC="${CC:-gcc}"
        BLOC_CCID="$(_bloc_ccid_of "$BLOC_CC")"
        BLOC_REQUIRED_TOOLS="$BLOC_CC cmake ninja"
        ;;
    host-gcc-m32)
        BLOC_KIND=host
        BLOC_CC=gcc
        BLOC_TARGET_FLAGS="-m32"
        BLOC_EXPECT_BIG_ENDIAN=0
        BLOC_EXPECT_PTR_BITS=32
        BLOC_REQUIRED_TOOLS="gcc cmake ninja"
        # gcc-multilib conflicts with the Linux cross GCCs, so it is often missing locally.
        if ! printf 'int main(void){return 0;}\n' | gcc -m32 -x c - -o /dev/null >/dev/null 2>&1; then
            BLOC_UNAVAILABLE="gcc -m32 cannot link here (gcc-multilib is not installed)"
        fi
        ;;
    aarch64) _bloc_emulated "$1" aarch64-linux-gnu qemu-aarch64 0 64 ;;
    armhf) _bloc_emulated "$1" arm-linux-gnueabihf qemu-arm 0 32 ;;
    riscv64) _bloc_emulated "$1" riscv64-linux-gnu qemu-riscv64 0 64 ;;
    powerpc) _bloc_emulated "$1" powerpc-linux-gnu qemu-ppc 1 32 ;;
    s390x) _bloc_emulated "$1" s390x-linux-gnu qemu-s390x 1 64 ;;
    cm0plus-gcc) _bloc_baremetal gcc arm-none-eabi-gcc arm-none-eabi-nm arm-none-eabi-size "-mcpu=cortex-m0plus -mthumb" ;;
    cm3-gcc) _bloc_baremetal gcc arm-none-eabi-gcc arm-none-eabi-nm arm-none-eabi-size "-mcpu=cortex-m3 -mthumb" ;;
    cm4-gcc) _bloc_baremetal gcc arm-none-eabi-gcc arm-none-eabi-nm arm-none-eabi-size "-mcpu=cortex-m4 -mthumb -mfloat-abi=soft" ;;
    ca7-gcc) _bloc_baremetal gcc arm-none-eabi-gcc arm-none-eabi-nm arm-none-eabi-size "-mcpu=cortex-a7 -mthumb -mfloat-abi=soft" ;;
    cr5-gcc) _bloc_baremetal gcc arm-none-eabi-gcc arm-none-eabi-nm arm-none-eabi-size "-mcpu=cortex-r5 -mthumb -mfloat-abi=soft" ;;
    cm0plus-clang)
        _bloc_baremetal clang clang arm-none-eabi-nm arm-none-eabi-size "--target=thumbv6m-none-eabi -mcpu=cortex-m0plus"
        BLOC_ISYSTEM="$_bloc_newlib"
        ;;
    cm3-clang)
        _bloc_baremetal clang clang arm-none-eabi-nm arm-none-eabi-size "--target=thumbv7m-none-eabi -mcpu=cortex-m3"
        BLOC_ISYSTEM="$_bloc_newlib"
        ;;
    rv32imac-clang)
        _bloc_baremetal clang clang riscv64-unknown-elf-nm riscv64-unknown-elf-size "--target=riscv32-unknown-elf -march=rv32imac -mabi=ilp32"
        BLOC_ISYSTEM="$_bloc_picolibc"
        ;;
    rv32imac-gcc)
        _bloc_baremetal gcc riscv64-unknown-elf-gcc riscv64-unknown-elf-nm riscv64-unknown-elf-size "-march=rv32imac -mabi=ilp32"
        BLOC_ISYSTEM="$_bloc_picolibc"
        ;;
    rv32i-gcc)
        _bloc_baremetal gcc riscv64-unknown-elf-gcc riscv64-unknown-elf-nm riscv64-unknown-elf-size "-march=rv32i -mabi=ilp32"
        BLOC_ISYSTEM="$_bloc_picolibc"
        ;;
    rv64imac-gcc)
        _bloc_baremetal gcc riscv64-unknown-elf-gcc riscv64-unknown-elf-nm riscv64-unknown-elf-size "-march=rv64imac -mabi=lp64"
        BLOC_ISYSTEM="$_bloc_picolibc"
        ;;
    atmega328p-gcc) _bloc_baremetal gcc avr-gcc avr-nm avr-size "-mmcu=atmega328p" ;;
    *)
        echo "unknown target: $1" >&2
        return 1
        ;;
    esac
    if [ "$BLOC_KIND" = baremetal ] && [ -n "$BLOC_ISYSTEM" ] && [ ! -d "$BLOC_ISYSTEM" ]; then
        BLOC_UNAVAILABLE="C library headers not found: $BLOC_ISYSTEM"
    fi
    return 0
}

# Library warning flags of section 3.3 for a compiler id (gcc | clang).
bloc_library_warning_flags() {
    local flags="-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow"
    flags="$flags -Wundef -Wvla -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes"
    flags="$flags -Wcast-align -fno-common"
    if [ "$1" = gcc ]; then
        flags="$flags -Wcast-align=strict"
    else
        # Clang lowers a memset call to __aeabi_memclr and a memcpy call to __aeabi_memcpy on ARM
        # EABI targets; R-03 allows only memcpy and memset. The same flags are set in
        # CMakeLists.txt.
        flags="$flags -fno-builtin-memset -fno-builtin-memcpy"
    fi
    echo "$flags"
}

# Writes a debug configuration that uses the default BLOC_PLATFORM_ASSERT and no test hooks
# (NH-02, XC-02). Usage: bloc_write_defassert_header <directory>; the file is cfg_debug_defassert.h.
bloc_write_defassert_header() {
    mkdir -p "$1"
    cat >"$1/cfg_debug_defassert.h" <<'HDR'
#define BLOC_BLOCK_ALIGNMENT 4
#define BLOC_PAYLOAD_ALIGNMENT 4
#define BLOC_SIZE_T uint16_t
#define BLOC_COUNT_T uint8_t
#define BLOC_REFCOUNT_T uint8_t
#define BLOC_CHECKS 1
#define BLOC_DEBUG 1
#define BLOC_STATS 1
HDR
}
