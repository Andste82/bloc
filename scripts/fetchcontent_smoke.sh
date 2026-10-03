#!/usr/bin/env bash
# FetchContent consumer smoke tests (section 9.8, FC-01..FC-10).
#
#   fetchcontent_smoke.sh [<variant>...]
#
# Variants: basic config-normal config-cache mismatch cstd generators cmake-floor baremetal docs
# online. Without arguments all offline variants run (everything except `online`). Each variant
# uses its own build directories under build/fc/ and, except `online`, consumes the current
# checkout through FETCHCONTENT_SOURCE_DIR_BLOC, so no network access is needed.
#
# Environment: BLOC_GIT_TAG (online: the commit to fetch), BLOC_FLOOR_VENV (a Python venv that
# has, or gets, cmake 3.20; default build/fc/venv-cmake-floor).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

smoke="$root/test/fetchcontent_smoke"
fc="$root/build/fc"
cmake_bin="cmake"
ctest_bin="ctest"
mkdir -p "$fc"

# The smoke program exercises the library only once main.c calls into it; until then the
# run-time sensitivity checks (FC-04, FC-08 symbol check) cannot work and are reported as SKIP.
smoke_uses_library() {
    grep -q 'bloc_pool_init' "$smoke/main.c"
}

say() { echo "== $*"; }

# configure <dir> [cmake args...]: configure the smoke project into a directory.
configure() {
    local dir="$1"
    shift
    "$cmake_bin" -S "$smoke" -B "$dir" "-DFETCHCONTENT_BASE_DIR=$dir/_deps" "$@"
}

offline_args() {
    echo "-DFETCHCONTENT_SOURCE_DIR_BLOC=$root"
}

# check_ctest_list <dir> <expected test names...>: ctest -N lists exactly these tests (FC-02).
check_ctest_list() {
    local dir="$1" listed expected
    shift
    listed="$("$ctest_bin" --test-dir "$dir" -N | sed -n 's/^ *Test *#[0-9]*: *//p' | sort | tr '\n' ' ')"
    expected="$(printf '%s\n' "$@" | sort | tr '\n' ' ')"
    if [ "$listed" != "$expected" ]; then
        echo "FC-02: ctest -N lists '$listed', expected '$expected'" >&2
        return 1
    fi
}

# build_and_test <dir> [ctest args...]
build_and_test() {
    local dir="$1"
    shift
    "$cmake_bin" --build "$dir"
    "$ctest_bin" --test-dir "$dir" --output-on-failure "$@"
}

fresh() { # fresh <dir>
    rm -rf "$1"
}

variant_basic() { # FC-01
    local dir="$fc/basic${1:-}"
    fresh "$dir"
    # shellcheck disable=SC2046
    configure "$dir" -G Ninja $(offline_args)
    build_and_test "$dir"
    check_ctest_list "$dir" smoke
}

variant_config() { # FC-03: variant_config <normal|cache> [suffix]
    local mode="$1" dir="$fc/config-$1${2:-}" rel
    fresh "$dir"
    # Configured twice: fresh, then reconfigure. Both must give the same result.
    # shellcheck disable=SC2046
    configure "$dir" -G Ninja $(offline_args) "-DSMOKE_CONFIG_MODE=$mode"
    # shellcheck disable=SC2046
    configure "$dir" -G Ninja $(offline_args) "-DSMOKE_CONFIG_MODE=$mode"
    build_and_test "$dir"
    check_ctest_list "$dir" smoke
    # A relative BLOC_CONFIG_DIRS must fail with the documented message.
    rel="$fc/config-$mode-relative"
    fresh "$rel"
    local out
    # shellcheck disable=SC2046
    if out="$(configure "$rel" -G Ninja $(offline_args) "-DSMOKE_CONFIG_MODE=$mode" \
        -DSMOKE_CONFIG_DIRS=config 2>&1)"; then
        echo "FC-03: a relative BLOC_CONFIG_DIRS was accepted" >&2
        return 1
    fi
    if ! grep -qF 'BLOC_CONFIG_DIRS entries must be absolute paths' <<<"$out"; then
        echo "FC-03: configure failed without the documented message:" >&2
        echo "$out" >&2
        return 1
    fi
}

variant_mismatch() { # FC-04
    local dir="$fc/mismatch"
    fresh "$dir"
    # shellcheck disable=SC2046
    configure "$dir" -G Ninja $(offline_args) -DSMOKE_MISMATCH=ON
    "$cmake_bin" --build "$dir"
    if smoke_uses_library; then
        "$ctest_bin" --test-dir "$dir" --output-on-failure
    else
        echo "SKIP FC-04 run: main.c is macro-only (scaffolding), the layout cross-check that" \
            "detects the mismatch does not exist yet; configure and build were checked"
        "$ctest_bin" --test-dir "$dir" --output-on-failure -R '^smoke$'
    fi
    check_ctest_list "$dir" smoke smoke_mismatch
}

variant_cstd() { # FC-05
    local std dir
    for std in 11 17 23; do
        dir="$fc/cstd-$std"
        fresh "$dir"
        # shellcheck disable=SC2046
        configure "$dir" -G Ninja $(offline_args) "-DCMAKE_C_STANDARD=$std" \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        build_and_test "$dir"
        python3 - "$dir/compile_commands.json" "$std" <<'PY'
import json, sys
path, std = sys.argv[1], sys.argv[2]
entries = json.load(open(path))
bloc = [e for e in entries if e["file"].endswith("/src/bloc.c")]
main = [e for e in entries if e["file"].endswith("/main.c")]
if len(bloc) != 1 or len(main) != 1:
    sys.exit("FC-05: expected one entry each for src/bloc.c and main.c, got %d and %d" % (len(bloc), len(main)))
cmd = bloc[0]["command"]
if " -std=c11 " not in cmd + " ":
    sys.exit("FC-05: src/bloc.c is not compiled with -std=c11 under CMAKE_C_STANDARD=%s: %s" % (std, cmd))
print("FC-05: CMAKE_C_STANDARD=%s: bloc.c uses -std=c11, main.c uses %s" % (
    std, [t for t in main[0]["command"].split() if t.startswith("-std=")]))
PY
    done
}

variant_generators() { # FC-06
    local dir
    for gen in "Ninja" "Unix Makefiles"; do
        dir="$fc/generators-${gen// /-}"
        fresh "$dir"
        # shellcheck disable=SC2046
        configure "$dir" -G "$gen" $(offline_args)
        build_and_test "$dir"
        check_ctest_list "$dir" smoke
    done
    dir="$fc/generators-Ninja-Multi-Config"
    fresh "$dir"
    # shellcheck disable=SC2046
    configure "$dir" -G "Ninja Multi-Config" $(offline_args)
    for cfg in Debug Release; do
        "$cmake_bin" --build "$dir" --config "$cfg"
        "$ctest_bin" --test-dir "$dir" -C "$cfg" --output-on-failure
    done
    check_ctest_list "$dir" smoke
}

variant_cmake_floor() { # FC-07
    local venv="${BLOC_FLOOR_VENV:-$fc/venv-cmake-floor}"
    if [ ! -x "$venv/bin/cmake" ] || ! "$venv/bin/cmake" --version | head -n 1 | grep -q 'version 3\.20\.'; then
        rm -rf "$venv"
        python3 -m venv "$venv"
        "$venv/bin/pip" install --quiet "cmake==3.20.*"
    fi
    "$venv/bin/cmake" --version | head -n 1
    cmake_bin="$venv/bin/cmake"
    ctest_bin="$venv/bin/ctest"
    variant_basic -floor
    variant_config normal -floor
}

variant_baremetal() { # FC-08
    local dir="$fc/baremetal" elf
    if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
        echo "SKIP FC-08: arm-none-eabi-gcc is not installed"
        return 0
    fi
    fresh "$dir"
    # shellcheck disable=SC2046
    configure "$dir" -G Ninja $(offline_args) \
        "-DCMAKE_TOOLCHAIN_FILE=$smoke/toolchain-cortex-m0plus.cmake"
    "$cmake_bin" --build "$dir"
    elf="$dir/smoke"
    [ -f "$elf" ] || {
        echo "FC-08: $elf was not built" >&2
        return 1
    }
    arm-none-eabi-nm "$elf" | grep -q ' T main$' || {
        echo "FC-08: main is missing in $elf" >&2
        return 1
    }
    if smoke_uses_library; then
        arm-none-eabi-nm "$elf" | grep -q ' T bloc_pool_init$' || {
            echo "FC-08: bloc_pool_init is missing in $elf" >&2
            return 1
        }
    else
        echo "SKIP FC-08 symbol check: main.c is macro-only (scaffolding), so the ELF holds no" \
            "bloc_pool_init yet; the bare-metal configure, compile and link were checked"
    fi
}

block_of() { # extract the bloc-fetchcontent block (markers included) of a file
    awk '/# >>> bloc-fetchcontent/ {on=1} on {print} /# <<< bloc-fetchcontent/ {on=0}' "$1"
}

variant_docs() { # FC-10
    if [ ! -f "$root/README.md" ] || ! grep -q '# >>> bloc-fetchcontent' "$root/README.md"; then
        echo "SKIP FC-10: README.md has no bloc-fetchcontent block yet (it is written in phase 7)"
        return 0
    fi
    local a b
    a="$(block_of "$root/README.md")"
    # shellcheck disable=SC2016
    b="$(block_of "$smoke/CMakeLists.txt" | sed -e 's/\${BLOC_GIT_TAG}/v1.0.0/' -e 's/\${BLOC_GIT_SHALLOW}/TRUE/')"
    if [ "$a" != "$b" ]; then
        echo "FC-10: the README block differs from the tested block:" >&2
        diff <(echo "$a") <(echo "$b") >&2 || true
        return 1
    fi
}

variant_online() { # FC-09
    local dir="$fc/online"
    : "${BLOC_GIT_TAG:?BLOC_GIT_TAG must name the commit to fetch}"
    fresh "$dir"
    configure "$dir" -G Ninja "-DBLOC_GIT_TAG=$BLOC_GIT_TAG" -DBLOC_GIT_SHALLOW=FALSE
    build_and_test "$dir"
    check_ctest_list "$dir" smoke
}

run_variant() {
    local v="$1"
    say "fetchcontent: $v"
    case "$v" in
    basic) variant_basic ;;
    config-normal) variant_config normal ;;
    config-cache) variant_config cache ;;
    mismatch) variant_mismatch ;;
    cstd) variant_cstd ;;
    generators) variant_generators ;;
    cmake-floor) variant_cmake_floor ;;
    baremetal) variant_baremetal ;;
    docs) variant_docs ;;
    online) variant_online ;;
    *)
        echo "unknown variant: $v" >&2
        return 2
        ;;
    esac
}

# Internal: run one variant in its own process, so that `set -e` stays effective inside it (it
# would be ignored in a subshell that is part of an `if` condition).
if [ "${1:-}" = "--run" ]; then
    run_variant "${2:?variant name}"
    exit 0
fi

variants=("$@")
if [ "${#variants[@]}" -eq 0 ]; then
    variants=(basic config-normal config-cache mismatch cstd generators cmake-floor baremetal docs)
fi

rc=0
for v in "${variants[@]}"; do
    if "$0" --run "$v"; then
        echo "PASS fetchcontent $v"
    else
        echo "FAIL fetchcontent $v"
        rc=1
    fi
done
exit "$rc"
