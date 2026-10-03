#!/usr/bin/env bash
# No-heap and dependency checks (NH-01..03, EM-03).
#
#   check_no_heap.sh                  NH-03, then NH-01 and NH-02 with $CC (default gcc)
#   check_no_heap.sh --grep-only      NH-03 only (CI gate job)
#   check_no_heap.sh --target <name>  EM-03 for an emulated target of target_table.sh
# shellcheck source-path=SCRIPTDIR
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
# shellcheck source=ci/target_table.sh
source "$root/scripts/ci/target_table.sh"

mode=all
target=""
while [ $# -gt 0 ]; do
    case "$1" in
    --grep-only) mode="grep" ;;
    --target)
        mode=target
        target="${2:?--target needs a target name}"
        shift
        ;;
    -h | --help)
        sed -n '2,7p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
    *)
        echo "unknown argument: $1" >&2
        exit 2
        ;;
    esac
    shift
done

rc=0

# NH-03: only the allowed includes, none of the forbidden functions (even in comments).
check_grep() {
    local bad
    bad="$(grep -rnE '^[[:space:]]*#[[:space:]]*include' --include='*.[ch]' src |
        grep -vE '#[[:space:]]*include[[:space:]]+(<(stddef|stdint|stdbool|string)\.h>|"bloc(_opt)?\.h"|BLOC_CONFIG_HEADER)' ||
        true)"
    if [ -n "$bad" ]; then
        echo "NH-03 FAIL: forbidden #include:" >&2
        echo "$bad" >&2
        return 1
    fi
    bad="$(grep -rnwE 'malloc|calloc|realloc|aligned_alloc|memmove|alloca' --include='*.[ch]' src || true)"
    bad="$bad$(grep -rnE '\bfree\(' --include='*.[ch]' src || true)"
    if [ -n "$bad" ]; then
        echo "NH-03 FAIL: forbidden identifier:" >&2
        echo "$bad" >&2
        return 1
    fi
    echo "PASS NH-03 includes and identifiers of src/ (library sources and public headers)"
}

# Compiles src/bloc.c for a configuration and checks its undefined symbols.
# check_object <label> <cc> <nm> <ccid> <opt> <config-header-or-empty> <extra-include-dir>... -- <flags>
check_object() {
    local label="$1" cc="$2" nm="$3" ccid="$4" opt="$5" cfg="$6" hooks="$7" debug="$8"
    shift 8
    local dir="$root/build/nohp/$label"
    local flags allowed=" memcpy memset " out sym bad=""
    mkdir -p "$dir"
    read -r -a flags <<<"$(bloc_library_warning_flags "$ccid") $opt -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
    flags+=("-I$root/src/include" "$@")
    if [ -n "$cfg" ]; then
        flags+=("-DBLOC_CONFIG_HEADER=\"$cfg\"")
    fi
    if [ "$hooks" -eq 1 ]; then
        allowed="$allowed ts_assert_fail ts_lock_enter ts_lock_exit "
    fi
    if [ "$debug" -eq 1 ]; then
        allowed="$allowed __aeabi_uidiv __aeabi_uidivmod __udivsi3 __umodsi3 __udivmodhi4 __udivmodsi4 "
    fi
    if ! "$cc" "${flags[@]}" -c "$root/src/bloc.c" -o "$dir/bloc.o" >"$dir/compile.log" 2>&1; then
        cat "$dir/compile.log" >&2
        echo "FAIL $label: compile error" >&2
        return 1
    fi
    if ! out="$("$nm" -u "$dir/bloc.o" 2>&1)"; then
        echo "FAIL $label: nm failed: $out" >&2
        return 1
    fi
    while read -r sym; do
        [ -n "$sym" ] || continue
        case "$allowed" in
        *" $sym "*) ;;
        *) bad="$bad $sym" ;;
        esac
    done < <(echo "$out" | awk '{print $NF}')
    if [ -n "$bad" ]; then
        echo "FAIL $label: forbidden undefined symbols:$bad" >&2
        return 1
    fi
    echo "PASS $label: undefined symbols are a subset of the allowed set"
}

check_host() { # NH-01, NH-02
    local cc="${CC:-gcc}" ccid dir
    ccid="$(_bloc_ccid_of "$cc")"
    dir="$root/build/nohp/defassert"
    bloc_write_defassert_header "$dir"
    check_object "NH-01 host default ($cc)" "$cc" nm "$ccid" -O2 "" 0 0 || rc=1
    check_object "NH-01 host debug ($cc)" "$cc" nm "$ccid" -O2 cfg_debug.h 1 0 \
        "-I$root/test/configs" "-I$root/test/support" || rc=1
    check_object "NH-02 host debug with default assert ($cc)" "$cc" nm "$ccid" -O2 \
        cfg_debug_defassert.h 0 0 "-I$dir" || rc=1
}

check_target() { # EM-03
    local t="$1" dir opt
    bloc_target "$t"
    if [ "$BLOC_KIND" != emulated ]; then
        echo "--target expects an emulated target, got $t ($BLOC_KIND)" >&2
        exit 2
    fi
    if [ -n "$BLOC_UNAVAILABLE" ] || ! command -v "$BLOC_CC" >/dev/null 2>&1 ||
        ! command -v "$BLOC_NM" >/dev/null 2>&1; then
        if bloc_strict; then
            echo "FAIL EM-03 $t: toolchain not installed (every tool is required in strict mode)" >&2
            return 1
        fi
        echo "SKIP EM-03 $t: toolchain not installed"
        return 0
    fi
    dir="$root/build/nohp/$t-defassert"
    bloc_write_defassert_header "$dir"
    opt="-Os"
    check_object "EM-03 $t default (MinSizeRel)" "$BLOC_CC" "$BLOC_NM" gcc "$opt" "" 0 0 || rc=1
    # BLOC_DEBUG = 1 may reference the unsigned division and modulo helpers (R-03).
    check_object "EM-03 $t debug with default assert (MinSizeRel)" "$BLOC_CC" "$BLOC_NM" gcc \
        "$opt" cfg_debug_defassert.h 0 1 "-I$dir" || rc=1
}

case "$mode" in
grep) check_grep || rc=1 ;;
all)
    check_grep || rc=1
    check_host
    ;;
target) check_target "$target" || rc=1 ;;
esac
exit "$rc"
