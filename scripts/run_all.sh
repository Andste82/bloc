#!/usr/bin/env bash
# Everything CI does, locally (section 10.1, item 11).
#
#   run_all.sh [<tier>...]
#
# Tiers: gate host m32 emulated baremetal size coverage sanitize lto noheap fetchcontent.
# Without arguments every tier runs. A tier whose tools are not installed prints SKIP lines instead
# of failing, unless CI=true or BLOC_STRICT=1 (then it fails). The script continues after a failure (step() swallows it) and exits non-zero at the
# end if any step failed. Set BLOC_UNITY_SOURCE_DIR to a local Unity checkout to avoid network
# access.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root" || exit 1
# shellcheck source-path=SCRIPTDIR source=ci/target_table.sh
source "$root/scripts/ci/target_table.sh"

failed=()

step() { # step <description> <command>...
    local description="$1"
    shift
    echo "---- $description"
    if ! "$@"; then
        echo "FAIL $description"
        failed+=("$description")
    fi
}

# A tool that is not installed: SKIP locally, FAIL in strict mode (CI or BLOC_STRICT=1).
skip() { # <description> <reason>
    if bloc_strict; then
        echo "FAIL $1: $2 (every tool is required in strict mode)"
        failed+=("$1")
    else
        echo "SKIP $1: $2"
    fi
}

have() { command -v "$1" >/dev/null 2>&1; }

tier_gate() {
    if have clang-format; then
        step "format" bash -c 'find src test examples \( -name "*.[ch]" -o -name "*.cpp" \) -print0 | xargs -0 clang-format --dry-run --Werror'
    else
        skip format "clang-format is not installed"
    fi
    if have shellcheck; then
        step "shellcheck" bash -c 'shellcheck -x scripts/*.sh scripts/ci/*.sh'
    else
        skip shellcheck "shellcheck is not installed"
    fi
    step "no-heap grep (NH-03)" scripts/check_no_heap.sh --grep-only
    step "build host default Debug, no test" scripts/ci/build_one.sh --no-test host default Debug
}

tier_host() {
    local cc cfg bt
    for cc in gcc clang; do
        if ! have "$cc"; then
            skip "host $cc" "not installed"
            continue
        fi
        for cfg in $BLOC_ALL_CONFIGS; do
            step "host $cc $cfg Debug" env CC="$cc" scripts/ci/build_one.sh host "$cfg" Debug
        done
        for bt in Release MinSizeRel; do
            for cfg in default debug nochecks; do
                step "host $cc $cfg $bt" env CC="$cc" scripts/ci/build_one.sh host "$cfg" "$bt"
            done
        done
    done
}

tier_m32() {
    local cfg
    for cfg in $BLOC_ALL_CONFIGS; do
        step "host-gcc-m32 $cfg Debug" scripts/ci/build_one.sh host-gcc-m32 "$cfg" Debug
    done
    step "host-gcc-m32 default Release" scripts/ci/build_one.sh host-gcc-m32 default Release
}

tier_emulated() {
    local t cfg bt
    for t in $BLOC_EMULATED_TARGETS; do
        for cfg in $BLOC_ALL_CONFIGS; do
            step "$t $cfg Debug" scripts/ci/build_one.sh "$t" "$cfg" Debug
        done
        for cfg in default nochecks; do
            bt=MinSizeRel
            step "$t $cfg $bt" scripts/ci/build_one.sh "$t" "$cfg" "$bt"
        done
        step "$t symbols (EM-03)" scripts/check_no_heap.sh --target "$t"
    done
}

tier_baremetal() { step "cross_check" scripts/cross_check.sh; }
tier_size() { step "check_size" scripts/check_size.sh; }

tier_coverage() {
    if have gcovr; then
        step "coverage" scripts/coverage.sh
    else
        skip coverage "gcovr is not installed"
    fi
}

tier_sanitize() {
    local cc
    for cc in gcc clang; do
        if have "$cc"; then
            step "sanitize $cc" scripts/sanitize.sh --cc "$cc"
        else
            skip "sanitize $cc" "not installed"
        fi
    done
    if have clang; then
        step "sanitize clang TSan" scripts/sanitize.sh --cc clang --tsan pthread
    fi
}

tier_lto() {
    local cc cfg
    for cc in gcc clang; do
        if ! have "$cc"; then
            skip "lto $cc" "not installed"
            continue
        fi
        for cfg in default debug nochecks; do
            step "lto $cc $cfg" env CC="$cc" scripts/ci/build_one.sh --lto host "$cfg" Release
        done
    done
}

tier_noheap() { step "no-heap" scripts/check_no_heap.sh; }
tier_fetchcontent() { step "fetchcontent smoke tests" scripts/fetchcontent_smoke.sh; }

tiers=("$@")
if [ "${#tiers[@]}" -eq 0 ]; then
    tiers=(gate host m32 emulated baremetal size coverage sanitize lto noheap fetchcontent)
fi
for tier in "${tiers[@]}"; do
    case "$tier" in
    gate | host | m32 | emulated | baremetal | size | coverage | sanitize | lto | noheap | fetchcontent)
        echo "==== tier: $tier"
        "tier_$tier"
        ;;
    *)
        echo "unknown tier: $tier" >&2
        exit 2
        ;;
    esac
done

echo "==== summary"
if [ "${#failed[@]}" -gt 0 ]; then
    printf 'FAILED: %s\n' "${failed[@]}"
    exit 1
fi
echo "all steps passed (SKIP lines above are tools that are not installed; set BLOC_STRICT=1 to make them failures)"
