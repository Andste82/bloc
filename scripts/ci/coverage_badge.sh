#!/usr/bin/env bash
# Coverage badge for the README: coverage_badge.sh <dir with gcovr JSON summaries>
#
# Prints a shields.io endpoint document. The message is the lowest line, branch or function
# coverage over all configurations, because the gate (section 7.1) requires each of them on its own.
set -euo pipefail

dir="${1:?usage: coverage_badge.sh <dir with gcovr JSON summaries>}"
python3 - "$dir" <<'PY'
import glob, json, os, sys

files = sorted(glob.glob(os.path.join(sys.argv[1], "*.json")))
if not files:
    sys.exit("coverage_badge.sh: no JSON summaries in " + sys.argv[1])
low = 100.0
for name in files:
    with open(name) as f:
        summary = json.load(f)
    for key in ("line_percent", "branch_percent", "function_percent"):
        low = min(low, float(summary[key]))
color = "brightgreen" if low >= 100.0 else "green" if low >= 90.0 else "yellow" if low >= 75.0 else "red"
text = "100%" if low >= 100.0 else "%.1f%%" % low
print(json.dumps({"schemaVersion": 1, "label": "coverage", "message": text, "color": color}))
PY
