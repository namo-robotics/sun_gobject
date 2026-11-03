#!/usr/bin/env bash
# Install the published development Debian package; never build the compiler.
set -euo pipefail

# Respect an explicitly selected compiler, including when updating.
if [[ -n ${SUN_EXECUTABLE:-} ]]; then
  "$SUN_EXECUTABLE" --version
  exit 0
fi

if command -v sun >/dev/null && [[ ${1:-} != --update ]]; then
  sun --version
  exit 0
fi

# Installation uses the published Debian package.
if [[ $(dpkg --print-architecture) != amd64 ]]; then
  echo 'The Sun dev Debian release currently supports amd64 only.' >&2
  exit 1
fi

mkdir -p "$PWD/tmp"
package_dir=$(mktemp -d "$PWD/tmp/sun-deb.XXXXXX")
trap 'rm -rf "$package_dir"' EXIT
chmod 755 "$package_dir"

curl --fail --location --retry 3 \
  https://github.com/namo-robotics/sun/releases/download/dev/sun_0.dev_amd64.deb \
  -o "$package_dir/sun.deb"
sha256sum "$package_dir/sun.deb"

admin=()
if [[ $(id -u) != 0 ]]; then
  admin=(sudo)
fi
"${admin[@]}" apt-get update
"${admin[@]}" apt-get install -y "$package_dir/sun.deb"
sun --version
