#!/bin/sh
set -eu

# Deprecated launcher. Universal 2 is produced by passing both input trees to
# the audited staging/signing/notarization tool; reject the obsolete notarize
# switch because credentials now come from a notarytool keychain profile.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "${1-}" = notarize ]; then
  echo "Legacy syntax is no longer supported; pass both --input values and --notary-profile to scripts/macos_bundle.py." >&2
  exit 2
fi
exec python3 "$repo_dir/scripts/macos_bundle.py" "$@"
