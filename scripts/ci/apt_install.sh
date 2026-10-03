#!/usr/bin/env bash
# apt-get with retries, used by every CI job: apt_install.sh <package>...
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
packages=(ca-certificates git "$@")

sudo=""
if [ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1; then
    sudo="sudo"
fi

attempts=3
for attempt in $(seq 1 "$attempts"); do
    if $sudo apt-get update && $sudo apt-get install -y --no-install-recommends "${packages[@]}"; then
        exit 0
    fi
    if [ "$attempt" -lt "$attempts" ]; then
        echo "::warning::apt-get attempt $attempt of $attempts failed, retrying in 15 s"
        sleep 15
    fi
done
echo "::error::apt-get failed after $attempts attempts: ${packages[*]}"
exit 1
