#!/usr/bin/env bash
# Code footprint report and gates (section 9.5, FP-01..FP-05).
#
#   check_size.sh [--family <name>] [--write-baseline]
#
# Compiles src/bloc.c with -Os -ffunction-sections -fdata-sections for every bare-metal target and
# a set of configurations, and writes a Markdown table of .text, .rodata, .data and .bss to
# build/size/report.md (build/size/report-<family>.md with --family). The table is also printed.
# Gates (the script exits non-zero if one fails):
#   FP-02  spec budgets for default and nochecks on cortex-m0plus and cortex-m3 (arm-none-eabi-gcc)
#   FP-03  .data and .bss are 0 everywhere; .rodata is 0 where BLOC_DEBUG = 0
#   FP-04  .text must not exceed scripts/size_baseline.txt (rows measured with another compiler
#          version are reported but not gated; a smaller value prints a reminder)
#   FP-05  dead stripping: test/size/min_app.c linked for cortex-m0plus keeps no other API function
# --write-baseline rewrites scripts/size_baseline.txt from the measured rows (full run only).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
# shellcheck source-path=SCRIPTDIR source=ci/target_table.sh
source "$root/scripts/ci/target_table.sh"

family=""
write_baseline=0
while [ $# -gt 0 ]; do
    case "$1" in
    --family)
        family="${2:?--family needs a name}"
        shift
        ;;
    --write-baseline) write_baseline=1 ;;
    -h | --help)
        sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
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

baseline="$root/scripts/size_baseline.txt"
rows="$outdir/rows.tsv"
if [ "$write_baseline" -eq 1 ] && [ -n "$family" ]; then
    echo "--write-baseline needs a full run (no --family)" >&2
    exit 2
fi
: >"$rows"

LAST_TEXT=0
default_text=0
rc=0
fail() {
    echo "FAIL footprint: $*" >&2
    rc=1
}

# Compiler version as one token, for the baseline key.
compiler_version() { # compiler
    "$1" --version 2>&1 | head -n 1 | tr ' ' '_'
}

# Spec section 17 budgets (FP-02): "target config budget".
budget_of() { # target config
    case "$1:$2" in
    cm0plus-gcc:default) echo 1536 ;;
    cm0plus-gcc:nochecks) echo 1152 ;;
    cm3-gcc:default) echo 1280 ;;
    cm3-gcc:nochecks) echo 960 ;;
    *) echo "" ;;
    esac
}

# FP-02, FP-03 and FP-04 for one measured row.
gate_row() { # target cc cfg text rodata data bss
    local target="$1" cc="$2" cfg="$3" text="$4" rodata="$5" data="$6" bss="$7"
    local budget version base_version base_text
    version="$(compiler_version "$cc")"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$target" "$cc" "$cfg" "$version" "$text" \
        "$rodata" "$data" "$bss" >>"$rows"
    if [ "$data" -ne 0 ] || [ "$bss" -ne 0 ]; then
        fail "FP-03 $target $cfg: .data=$data .bss=$bss (both must be 0)"
    fi
    case "$cfg" in
    debug-defassert) ;; # assertion messages may live in .rodata
    *)
        if [ "$rodata" -ne 0 ]; then
            fail "FP-03 $target $cfg: .rodata=$rodata (must be 0 with BLOC_DEBUG = 0)"
        fi
        ;;
    esac
    budget="$(budget_of "$target" "$cfg")"
    if [ -n "$budget" ] && [ "$text" -gt "$budget" ]; then
        fail "FP-02 $target $cfg: .text=$text exceeds the budget of $budget bytes"
    fi
    if [ "$write_baseline" -eq 1 ]; then
        return 0
    fi
    if [ ! -f "$baseline" ]; then
        fail "FP-04 scripts/size_baseline.txt is missing (create it with --write-baseline)"
        return 0
    fi
    read -r base_version base_text <<<"$(awk -v t="$target" -v c="$cc" -v g="$cfg" \
        '$1 == t && $2 == c && $3 == g { print $4, $5 }' "$baseline")"
    if [ -z "${base_text:-}" ]; then
        fail "FP-04 $target $cc $cfg has no row in scripts/size_baseline.txt"
    elif [ "$base_version" != "$version" ]; then
        echo "note FP-04 $target $cfg: compiler version differs from the baseline ($version" \
            "vs $base_version), row not gated" >&2
    elif [ "$text" -gt "$base_text" ]; then
        fail "FP-04 $target $cfg: .text=$text is larger than the baseline $base_text"
    elif [ "$text" -lt "$base_text" ]; then
        echo "note FP-04 $target $cfg: .text=$text is below the baseline $base_text;" \
            "lower the baseline (check_size.sh --write-baseline) in this commit" >&2
    fi
}

# FP-05: link test/size/min_app.c for ARMv6-M and check which BLOC functions survive.
dead_strip_check() { # target full-api-text
    local target="$1" full="$2" dir="$outdir/$1/deadstrip" elf names kept sym size total=0
    local public="bloc_pool_deinit bloc_pool_free_count bloc_pool_get_stats bloc_calloc bloc_retain
        bloc_data bloc_len bloc_headroom bloc_tailroom bloc_set_len bloc_add_header
        bloc_remove_header bloc_copy_from bloc_copy_to bloc_copy bloc_append bloc_append_data
        bloc_prepend bloc_prepend_data"
    mkdir -p "$dir"
    elf="$dir/min_app.elf"
    local target_flags
    read -r -a target_flags <<<"$BLOC_TARGET_FLAGS"
    if ! "$BLOC_CC" "${target_flags[@]}" -std=c11 -Os -ffunction-sections -fdata-sections \
        -I"$root/include" "$root/src/bloc.c" "$root/test/size/min_app.c" -o "$elf" \
        -Wl,--gc-sections --specs=nosys.specs >"$dir/link.log" 2>&1; then
        cat "$dir/link.log" >&2
        fail "FP-05 $target: min_app does not link"
        return 0
    fi
    names="$("$BLOC_NM" -S "$elf" | awk '$3 ~ /^[tT]$/ && $4 ~ /^bloc_/ { print $4 }')"
    for sym in bloc_pool_init bloc_alloc bloc_release; do
        if ! grep -qx "$sym" <<<"$names"; then
            fail "FP-05 $target: $sym is missing from the linked min_app"
        fi
    done
    kept=""
    for sym in $public; do
        if grep -qx "$sym" <<<"$names"; then
            kept="$kept $sym"
        fi
    done
    if [ -n "$kept" ]; then
        fail "FP-05 $target: functions that min_app does not call survived:$kept"
    fi
    while read -r size sym; do
        total=$((total + 0x$size))
    done < <("$BLOC_NM" -S "$elf" | awk '$3 ~ /^[tT]$/ && $4 ~ /^bloc_/ { print $2, $4 }')
    if [ "$total" -ge "$full" ]; then
        fail "FP-05 $target: min_app contribution $total is not below the full API ($full)"
    fi
    echo "PASS FP-05 $target: min_app keeps only the called functions ($total of $full bytes)"
}

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
    gate_row "$target" "$BLOC_CC" "$cfg" "$text" "$rodata" "$data" "$bss"
    LAST_TEXT="$text"
    case "$target" in
    cm* | ca* | cr*)
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
        if [ "$cfg" = default ]; then
            default_text="$LAST_TEXT"
        fi
    done
    case "$t" in
    cm0plus-* | cm3-* | cm4-*) measure "$t" size_ts size_ts ;;
    esac
    if [ "$t" = cm0plus-gcc ]; then
        dead_strip_check "$t" "$default_text"
    fi
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

if [ "$write_baseline" -eq 1 ]; then
    {
        echo "# Footprint regression baseline (FP-04): target compiler configuration compiler-version .text"
        echo "# Written by scripts/check_size.sh --write-baseline; only ever lower it, or justify a rise."
        # The re-measured rows are identical; keep the first of each key.
        awk -F'\t' '!seen[$1 FS $2 FS $3]++ { print $1, $2, $3, $4, $5 }' "$rows"
    } >"$baseline"
    echo "wrote $baseline" >&2
fi

cat "$report"
cat "$details"
if [ -n "$family" ]; then
    cp "$details" "$outdir/details-$family.md"
fi
exit "$rc"
