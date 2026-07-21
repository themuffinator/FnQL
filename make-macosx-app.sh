#!/bin/sh
set -eu

# Deprecated launcher for the maintained Meson-install-tree packager. The old
# target/architecture interface assembled incomplete bundles and is rejected
# with a migration hint instead of being silently misinterpreted.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case ${1-} in
  release|debug)
    echo "Legacy syntax is no longer supported; use scripts/macos_bundle.py --input ARCH=PATH --output DIR." >&2
    exit 2
    ;;
esac
exec python3 "$repo_dir/scripts/macos_bundle.py" "$@"
