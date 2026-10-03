#!/usr/bin/env bash
# Sanitizer runs (section 9.2): SAN-01..03.
#
#   sanitize.sh [--cc gcc|clang] [--tsan] [<config>...]
#
# Default: ASan + UBSan over every coverage-gated configuration. With --tsan the pthread
# configuration is built with ThreadSanitizer instead. Set BLOC_UNITY_SOURCE_DIR to a local Unity
# checkout to configure without network access.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
# shellcheck source-path=SCRIPTDIR source=ci/target_table.sh
source "$root/scripts/ci/target_table.sh"

cc="${CC:-gcc}"
tsan=0
configs=()
while [ $# -gt 0 ]; do
    case "$1" in
    --cc)
        cc="${2:?--cc needs gcc or clang}"
        shift
        ;;
    --tsan) tsan=1 ;;
    -h | --help)
        sed -n '2,8p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
    *) configs+=("$1") ;;
    esac
    shift
done
case "$cc" in
gcc | clang) ;;
*)
    echo "--cc must be gcc or clang" >&2
    exit 2
    ;;
esac

if [ "${#configs[@]}" -eq 0 ]; then
    if [ "$tsan" -eq 1 ]; then
        configs=(pthread)
    else
        read -r -a configs <<<"$BLOC_GATED_CONFIGS"
    fi
fi

extra=()
if [ -n "${BLOC_UNITY_SOURCE_DIR:-}" ]; then
    extra+=("-DFETCHCONTENT_SOURCE_DIR_UNITY=$BLOC_UNITY_SOURCE_DIR")
fi

for cfg in "${configs[@]}"; do
    preset="san-$cc-$cfg"
    echo "== sanitize: $preset"
    if [ "$tsan" -eq 1 ] && [ "$cfg" != pthread ]; then
        echo "--tsan applies to the pthread configuration only" >&2
        exit 2
    fi
    cmake --preset "$preset" "${extra[@]}"
    cmake --build --preset "$preset"
    if [ "$tsan" -eq 1 ] && setarch "$(uname -m)" -R true >/dev/null 2>&1; then
        # Recent kernels with a high vm.mmap_rnd_bits value (32) break the ThreadSanitizer memory
        # layout unless ASLR is disabled for the test processes.
        setarch "$(uname -m)" -R ctest --preset "$preset"
    elif ! ctest --preset "$preset"; then
        if [ "$tsan" -eq 1 ]; then
            echo "note: if the failures say 'incompatible memory layout', lower vm.mmap_rnd_bits" \
                "(sysctl vm.mmap_rnd_bits=28) or run where setarch -R is permitted" >&2
        fi
        exit 1
    fi
done
