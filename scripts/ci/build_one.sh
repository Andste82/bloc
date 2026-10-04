#!/usr/bin/env bash
# Build (and test) one target x configuration x build type.
#
#   build_one.sh [--no-test] [--lto] <selector> <config> [Debug|Release|MinSizeRel]
#   build_one.sh --versions <selector>
#
# <selector> is a target name from target_table.sh or "family:<name>". Host and emulated targets
# are configured with CMake into build/ci/<target>[/<compiler>]/<config>-<type>[-lto], built and run through ctest.
# Bare-metal targets are checked without CMake: the single translation unit is compiled directly
# (XC-01, XC-03), its undefined symbols are checked (XC-02), the static layout check is compiled
# (XC-05) and expected failures are verified (XC-04).
#
# Every combination prints one line: PASS|FAIL|SKIP <target> <config> <type>.
# In CI (CI=true, GITHUB_ACTIONS=true) or with BLOC_STRICT=1 a missing tool or an unavailable target
# is a FAIL, not a SKIP; only a configuration that does not apply to a target is skipped.
# Set BLOC_UNITY_SOURCE_DIR to a local Unity checkout to configure without network access.
# shellcheck source-path=SCRIPTDIR
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
# shellcheck source=target_table.sh
source "$root/scripts/ci/target_table.sh"

usage() {
    sed -n '2,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 2
}

no_test=0
lto=0
versions=0
args=()
for a in "$@"; do
    case "$a" in
    --no-test) no_test=1 ;;
    --lto) lto=1 ;;
    --versions) versions=1 ;;
    -h | --help) usage ;;
    *) args+=("$a") ;;
    esac
done

tool_version() {
    if command -v "$1" >/dev/null 2>&1; then
        printf '%s: ' "$1"
        "$1" --version 2>&1 | head -n 1
    else
        echo "$1: not installed"
    fi
}

print_versions() {
    local t="$1" tool
    bloc_target "$t"
    echo "== $t"
    for tool in $BLOC_REQUIRED_TOOLS $BLOC_NM; do
        tool_version "$tool"
    done
    if [ "$BLOC_KIND" != baremetal ]; then
        tool_version cmake
        tool_version ninja
    fi
    if [ -n "$BLOC_EMULATOR" ]; then
        tool_version "$BLOC_EMULATOR"
    fi
}

targets_of() {
    case "$1" in
    family:*) bloc_family "${1#family:}" ;;
    *) echo "$1" ;;
    esac
}

if [ "$versions" -eq 1 ]; then
    [ "${#args[@]}" -eq 1 ] || usage
    for t in $(targets_of "${args[0]}"); do
        print_versions "$t"
    done
    exit 0
fi

[ "${#args[@]}" -ge 2 ] && [ "${#args[@]}" -le 3 ] || usage
selector="${args[0]}"
config="${args[1]}"
build_type="${args[2]:-Debug}"

case "$build_type" in
Debug | Release | MinSizeRel) ;;
*)
    echo "unknown build type: $build_type" >&2
    exit 2
    ;;
esac

# Runs a command with its output in a log file; prints the log if the command fails.
logged() {
    local log="$1"
    shift
    mkdir -p "$(dirname "$log")"
    if "$@" >"$log" 2>&1; then
        return 0
    fi
    echo "---- command failed: $*" >&2
    cat "$log" >&2
    echo "---- end of log ($log)" >&2
    return 1
}

last_build_dir=""

# "N tests" from the ctest log of the current build, plus a note when the C++ consumer test
# (CFG-12) was not built. Empty with --no-test.
ctest_summary() {
    local log="$last_build_dir/ctest.log" n
    [ "$no_test" -eq 0 ] && [ -f "$log" ] || return 0
    n="$(sed -n 's/.*tests failed out of \([0-9][0-9]*\).*/\1/p' "$log" | tail -n 1)"
    if grep -q 'test_cpp' "$log"; then
        echo "${n:-?} tests"
    else
        echo "${n:-?} tests, no C++ test"
    fi
}

report() { # status target config type [note]
    printf '%s %s %s %s%s\n' "$1" "$2" "$3" "$4" "${5:+ ($5)}"
}

missing_tool() {
    local tool
    for tool in $BLOC_REQUIRED_TOOLS; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "$tool"
            return 0
        fi
    done
    return 1
}

build_cmake() { # host and emulated targets
    local target="$1" suffix="" dir cdir=""
    [ "$lto" -eq 1 ] && suffix="-lto"
    # Host builds take their compiler from $CC, so each compiler needs its own directory:
    # CMake discards the cache (and with it -DBLOC_TEST_CONFIG etc.) when the compiler changes.
    [ "$BLOC_KIND" = emulated ] || cdir="/$(basename "$BLOC_CC")"
    dir="$root/build/ci/$target$cdir/$config-$build_type$suffix"
    last_build_dir="$dir" # read by ctest_summary
    local cfg_args=(-S "$root" -B "$dir" -G Ninja "-DCMAKE_BUILD_TYPE=$build_type"
        "-DBLOC_TEST_CONFIG=$config")
    if [ "$BLOC_KIND" = emulated ]; then
        cfg_args+=("-DCMAKE_TOOLCHAIN_FILE=$root/$BLOC_TOOLCHAIN_FILE")
    else
        cfg_args+=("-DCMAKE_C_COMPILER=$BLOC_CC")
        if [ -n "$BLOC_TARGET_FLAGS" ]; then
            # The same flags for the C++ consumer test (CFG-12), e.g. -m32.
            cfg_args+=("-DCMAKE_C_FLAGS=$BLOC_TARGET_FLAGS" "-DCMAKE_CXX_FLAGS=$BLOC_TARGET_FLAGS")
        fi
    fi
    if [ -n "$BLOC_EXPECT_PTR_BITS" ]; then
        cfg_args+=("-DBLOC_EXPECT_BIG_ENDIAN=$BLOC_EXPECT_BIG_ENDIAN"
            "-DBLOC_EXPECT_PTR_BITS=$BLOC_EXPECT_PTR_BITS")
    fi
    if [ "$lto" -eq 1 ]; then
        cfg_args+=(-DBLOC_LTO=ON)
    fi
    if [ -n "${BLOC_UNITY_SOURCE_DIR:-}" ]; then
        cfg_args+=("-DFETCHCONTENT_SOURCE_DIR_UNITY=$BLOC_UNITY_SOURCE_DIR")
    fi
    logged "$dir/configure.log" cmake "${cfg_args[@]}" || return 1
    local got
    got="$(sed -n 's/^BLOC_TEST_CONFIG:[A-Z]*=//p' "$dir/CMakeCache.txt")"
    if [ "$got" != "$config" ]; then
        echo "CMake cache of $dir has BLOC_TEST_CONFIG='$got', expected '$config'" >&2
        return 1
    fi
    if [ "$BLOC_KIND" != emulated ]; then
        got="$(sed -n 's/^CMAKE_C_COMPILER:[A-Z]*=//p' "$dir/CMakeCache.txt")"
        if [ "$(basename "$got")" != "$(basename "$BLOC_CC")" ]; then
            echo "CMake cache of $dir uses compiler '$got', expected '$BLOC_CC'" >&2
            return 1
        fi
    fi
    logged "$dir/build.log" cmake --build "$dir" || return 1
    if [ "$no_test" -eq 0 ]; then
        logged "$dir/ctest.log" ctest --test-dir "$dir" --output-on-failure || return 1
    fi
    return 0
}

# Undefined symbols of an object that are not allowed (XC-02). Prints the offenders.
bad_undefined_symbols() { # object debug(0|1) config-header-or-empty
    local allowed=" memcpy memset "
    # Only the test hooks that the configuration header really uses are allowed.
    if [ -n "$3" ]; then
        if grep -q 'ts_assert_fail' "$3"; then
            allowed="$allowed ts_assert_fail "
            # avr-gcc keeps string literals in RAM and marks an object that has some with the
            # startup symbol __do_copy_data (spec section 2). The assertion messages of a debug
            # build only become literals when a test hook receives them and
            # BLOC_ASSERT_MESSAGES is not 0; the default assertion discards them.
            if ! grep -qE '^#define BLOC_ASSERT_MESSAGES 0' "$3"; then
                case "$BLOC_CC" in
                *avr-gcc*) [ "$2" -eq 1 ] && allowed="$allowed __do_copy_data " ;;
                esac
            fi
        fi
        grep -q 'ts_lock_enter' "$3" && allowed="$allowed ts_lock_enter "
        grep -q 'ts_lock_exit' "$3" && allowed="$allowed ts_lock_exit "
    fi
    if [ "$2" -eq 1 ]; then
        allowed="$allowed __aeabi_uidiv __aeabi_uidivmod __udivsi3 __umodsi3 __udivmodhi4 __udivmodsi4 "
    fi
    local sym out
    if ! out="$("$BLOC_NM" -u "$1" 2>&1)"; then
        echo "nm failed on $1: $out"
        return 0
    fi
    echo "$out" | awk '{print $NF}' | while read -r sym; do
        case "$allowed" in
        *" $sym "*) ;;
        *) echo "$sym" ;;
        esac
    done
}

build_baremetal() {
    local target="$1" dir="$root/build/ci/$1/$config"
    local flags cfg_flags=() expected msg debug_cfg=0 cc_flags cfg_header=""
    mkdir -p "$dir"
    flags="$(bloc_library_warning_flags "$BLOC_CCID") -Os -ffunction-sections -fdata-sections"
    flags="$flags $BLOC_TARGET_FLAGS"
    if [ -n "$BLOC_ISYSTEM" ]; then
        flags="$flags -isystem $BLOC_ISYSTEM"
    fi
    flags="$flags -I$root/src/include"
    if [ "$config" != default ]; then
        cfg_flags=("-DBLOC_CONFIG_HEADER=\"cfg_$config.h\"" "-I$root/test/configs"
            "-I$root/test/support")
        cfg_header="$root/test/configs/cfg_$config.h"
        if grep -q '^#define BLOC_DEBUG 1' "$cfg_header"; then
            debug_cfg=1
        fi
    fi
    read -r -a cc_flags <<<"$flags"

    expected="$(bloc_expected_failure "$target" "$config")"
    if [ -n "$expected" ]; then
        if "$BLOC_CC" "${cc_flags[@]}" "${cfg_flags[@]}" -c "$root/src/bloc.c" -o "$dir/bloc.o" \
            >"$dir/compile.log" 2>&1; then
            echo "expected a compile error but the build succeeded" >&2
            return 1
        fi
        if ! grep -qF "$expected" "$dir/compile.log"; then
            echo "compile failed, but without the expected message: $expected" >&2
            cat "$dir/compile.log" >&2
            return 1
        fi
        return 0
    fi

    logged "$dir/compile.log" "$BLOC_CC" "${cc_flags[@]}" "${cfg_flags[@]}" -c "$root/src/bloc.c" \
        -o "$dir/bloc.o" || return 1
    msg="$(bad_undefined_symbols "$dir/bloc.o" "$debug_cfg" "$cfg_header")"
    if [ -n "$msg" ]; then
        echo "forbidden undefined symbols in $dir/bloc.o:" >&2
        echo "$msg" >&2
        return 1
    fi

    if [ "$config" = debug ]; then
        # Debug with the default BLOC_PLATFORM_ASSERT and no test hooks (NH-02 equivalent).
        bloc_write_defassert_header "$dir"
        logged "$dir/compile_defassert.log" "$BLOC_CC" "${cc_flags[@]}" \
            "-DBLOC_CONFIG_HEADER=\"cfg_debug_defassert.h\"" "-I$dir" -c "$root/src/bloc.c" \
            -o "$dir/bloc_defassert.o" || return 1
        msg="$(bad_undefined_symbols "$dir/bloc_defassert.o" 1 "")"
        if [ -n "$msg" ]; then
            echo "default assert build references forbidden symbols:" >&2
            echo "$msg" >&2
            return 1
        fi
    fi

    if [ -f "$root/test/target/layout_static.c" ]; then
        logged "$dir/layout_static.log" "$BLOC_CC" "${cc_flags[@]}" "${cfg_flags[@]}" -ffreestanding \
            -c "$root/test/target/layout_static.c" -o "$dir/layout_static.o" || return 1
    fi
    return 0
}

skip_or_fail() { # target reason
    if bloc_strict; then
        report FAIL "$1" "$config" "$build_type" "$2; every tool is required in strict mode"
        return 1
    fi
    report SKIP "$1" "$config" "$build_type" "$2"
    return 0
}

run_target() {
    local target="$1" tool
    if ! bloc_target "$target"; then
        report FAIL "$target" "$config" "$build_type" "unknown target"
        return 1
    fi
    case " $BLOC_ALL_CONFIGS " in
    *" $config "*) ;;
    *)
        report FAIL "$target" "$config" "$build_type" "unknown configuration"
        return 1
        ;;
    esac
    if [ -n "$BLOC_UNAVAILABLE" ]; then
        skip_or_fail "$target" "$BLOC_UNAVAILABLE"
        return
    fi
    if tool="$(missing_tool)"; then
        skip_or_fail "$target" "$tool is not installed"
        return
    fi
    case " $BLOC_CONFIGS " in
    *" $config "*) ;;
    *)
        report SKIP "$target" "$config" "$build_type" "configuration does not apply to this target"
        return 0
        ;;
    esac
    if [ "$BLOC_KIND" = baremetal ]; then
        if build_baremetal "$target"; then
            report PASS "$target" "$config" "$build_type"
            return 0
        fi
    elif build_cmake "$target"; then
        report PASS "$target" "$config" "$build_type" "$(ctest_summary)"
        return 0
    fi
    report FAIL "$target" "$config" "$build_type"
    return 1
}

rc=0
for t in $(targets_of "$selector"); do
    run_target "$t" || rc=1
done
exit "$rc"
