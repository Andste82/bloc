#!/usr/bin/env bash
# Code footprint report (section 9.5, FP-01). Informational until phase 4: it never gates.
#
#   check_size.sh [--family <name>]
#
# Compiles src/bloc.c with -Os -ffunction-sections -fdata-sections for every bare-metal target and
# a set of configurations, and writes a Markdown table of .text, .rodata, .data and .bss to
# build/size/report.md (build/size/report-<family>.md with --family). The table is also printed.
# The budgets (FP-02), section rules (FP-03), baseline (FP-04) and dead-strip check (FP-05) are
# added in phase 4.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
# shellcheck source-path=SCRIPTDIR source=ci/target_table.sh
source "$root/scripts/ci/target_table.sh"

family=""
while [ $# -gt 0 ]; do
    case "$1" in
    --family)
        family="${2:?--family needs a name}"
        shift
        ;;
    -h | --help)
        sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
    *)
        echo "unknown argument: $1" >&2
        exit 2
        ;;
    esac
    shift
done

outdir="$root/build/size"
mkdir -p "$outdir"
report="$outdir/report.md"
[ -n "$family" ] && report="$outdir/report-$family.md"

{
    echo "| target | compiler | configuration | .text | .rodata | .data | .bss |"
    echo "| --- | --- | --- | ---: | ---: | ---: | ---: |"
} >"$report"
details="$outdir/details.md"
: >"$details"

# Sums the section sizes of an object: prints "text rodata data bss".
sections() { # size-tool object
    "$1" -A "$2" | awk '
        $1 ~ /^\.text/   { t += $2 }
        $1 ~ /^\.rodata/ { r += $2 }
        $1 ~ /^\.data/   { d += $2 }
        $1 ~ /^\.bss/    { b += $2 }
        END { printf "%d %d %d %d\n", t, r, d, b }'
}

rc=0
measure() { # target label config
    local target="$1" label="$2" cfg="$3"
    local dir="$outdir/$target/$label" flags cfg_flags=() obj="$outdir/$target/$label/bloc.o"
    mkdir -p "$dir"
    flags="$(bloc_library_warning_flags "$BLOC_CCID") -Os -ffunction-sections -fdata-sections"
    flags="$flags $BLOC_TARGET_FLAGS"
    [ -n "$BLOC_ISYSTEM" ] && flags="$flags -isystem $BLOC_ISYSTEM"
    flags="$flags -I$root/include"
    local expected
    expected="$(bloc_expected_failure "$target" "$cfg")"
    if [ -n "$expected" ]; then
        return 0 # the configuration does not exist on this target (XC-04)
    fi
    case "$cfg" in
    default) ;;
    debug-defassert)
        bloc_write_defassert_header "$dir"
        cfg_flags=("-DBLOC_CONFIG_HEADER=\"cfg_debug_defassert.h\"" "-I$dir")
        ;;
    size_ts)
        cfg_flags=("-DBLOC_CONFIG_HEADER=\"cfg_size_ts.h\"" "-I$root/test/size")
        ;;
    *)
        cfg_flags=("-DBLOC_CONFIG_HEADER=\"cfg_$cfg.h\"" "-I$root/test/configs"
            "-I$root/test/support")
        ;;
    esac
    local cc_flags
    read -r -a cc_flags <<<"$flags"
    if ! "$BLOC_CC" "${cc_flags[@]}" "${cfg_flags[@]}" -c "$root/src/bloc.c" -o "$obj" \
        >"$dir/compile.log" 2>&1; then
        cat "$dir/compile.log" >&2
        echo "FAIL footprint: $target $cfg does not compile" >&2
        rc=1
        return 0
    fi
    local text rodata data bss
    read -r text rodata data bss <<<"$(sections "$BLOC_SIZE" "$obj")"
    printf '| %s | %s | %s | %s | %s | %s | %s |\n' "$target" "$BLOC_CC" "$cfg" "$text" "$rodata" \
        "$data" "$bss" >>"$report"
    case "$target" in
    cm*)
        {
            echo
            echo "Largest functions: $target, $cfg"
            echo
            echo '```text'
            "$BLOC_NM" --size-sort -S -t d "$obj" | grep -iE ' [tT] ' | tail -n 10 || true
            echo '```'
        } >>"$details"
        ;;
    esac
}

if [ -n "$family" ]; then
    targets="$(bloc_family "$family")"
else
    targets="$(bloc_all_baremetal | tr '\n' ' ')"
fi

for t in $targets; do
    bloc_target "$t"
    if [ -n "$BLOC_UNAVAILABLE" ] || ! command -v "$BLOC_CC" >/dev/null 2>&1 ||
        ! command -v "$BLOC_SIZE" >/dev/null 2>&1; then
        echo "SKIP footprint $t: toolchain not installed" >&2
        continue
    fi
    for cfg in default nochecks debug-defassert wide; do
        measure "$t" "$cfg" "$cfg"
    done
    case "$t" in
    cm0plus-* | cm3-* | cm4-*) measure "$t" size_ts size_ts ;;
    esac
done

if [ -z "$family" ]; then
    # Host x86-64 for information.
    BLOC_CC="${CC:-gcc}"
    if command -v "$BLOC_CC" >/dev/null 2>&1 && command -v size >/dev/null 2>&1; then
        BLOC_CCID="$(_bloc_ccid_of "$BLOC_CC")" BLOC_TARGET_FLAGS="" BLOC_ISYSTEM=""
        BLOC_SIZE=size
        for cfg in default nochecks debug-defassert wide; do
            measure host "$cfg" "$cfg"
        done
    fi
fi

cat "$report"
cat "$details"
if [ -n "$family" ]; then
    cp "$details" "$outdir/details-$family.md"
fi
exit "$rc"
