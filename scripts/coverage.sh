#!/usr/bin/env bash
# Coverage gate (section 7.1): build, test and gcovr gate for each coverage configuration.
#
#   coverage.sh [<config>...]     default: all six coverage-gated configurations
#
# Each configuration must reach 100 % line, branch and function coverage of src/ on its own.
# Exits non-zero on the first failure. HTML reports and JSON summaries are copied to
# build/coverage/<config>.html and <config>.json (the summaries feed the README badge).
# Set BLOC_UNITY_SOURCE_DIR to a local Unity checkout to configure without network access.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
# shellcheck source-path=SCRIPTDIR source=ci/target_table.sh
source "$root/scripts/ci/target_table.sh"

configs="$*"
[ -n "$configs" ] || configs="$BLOC_GATED_CONFIGS"

extra=()
if [ -n "${BLOC_UNITY_SOURCE_DIR:-}" ]; then
    extra+=("-DFETCHCONTENT_SOURCE_DIR_UNITY=$BLOC_UNITY_SOURCE_DIR")
fi

mkdir -p "$root/build/coverage"
for cfg in $configs; do
    echo "== coverage: $cfg"
    # Always a clean build: stale counters (.gcda) would hide missing coverage, and notes files
    # (.gcno) of objects that are no longer built, for example after a source moved to another
    # target, make gcovr see one source with two different line tables.
    if [ -d "$root/build/cov-$cfg" ]; then
        find "$root/build/cov-$cfg" \( -name '*.gcda' -o -name '*.gcno' -o -name '*.gcov' \) -delete
    fi
    cmake --preset "cov-$cfg" "${extra[@]}"
    cmake --build --preset "cov-$cfg" --clean-first
    ctest --preset "cov-$cfg"
    # The explicit search path keeps gcovr from scanning the repository root, which would merge
    # the counters of every other build directory into this report.
    gcovr --root . --filter 'src/' --object-directory "build/cov-$cfg" \
        --exclude-unreachable-branches \
        --fail-under-line 100 --fail-under-branch 100 --fail-under-function 100 \
        --txt --html-details "build/cov-$cfg/coverage.html" \
        --json-summary "build/cov-$cfg/summary.json" "build/cov-$cfg"
    cp "$root/build/cov-$cfg/coverage.html" "$root/build/coverage/$cfg.html"
    cp "$root/build/cov-$cfg/summary.json" "$root/build/coverage/$cfg.json"
done
