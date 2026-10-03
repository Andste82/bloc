#!/usr/bin/env bash
# Cross-compile matrix of the bare-metal targets (tier 2, section 9.4): XC-01..05.
#
#   cross_check.sh [--family <name>]
#
# Calls ci/build_one.sh for every bare-metal target x configuration and prints one line per
# combination. Exits non-zero if any combination fails; a missing compiler gives a visible SKIP.
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
        sed -n '2,8p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
    *)
        echo "unknown argument: $1" >&2
        exit 2
        ;;
    esac
    shift
done

if [ -n "$family" ]; then
    selectors="family:$family"
    bloc_family "$family" >/dev/null
else
    selectors="$(bloc_families | tr ' ' '\n' | sed 's/^/family:/')"
fi

rc=0
for sel in $selectors; do
    for cfg in $BLOC_ALL_CONFIGS; do
        "$root/scripts/ci/build_one.sh" "$sel" "$cfg" Debug || rc=1
    done
done
exit "$rc"
