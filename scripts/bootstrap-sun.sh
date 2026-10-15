#!/usr/bin/env bash
# Install the published development Debian package; never build the compiler.
set -euo pipefail
if [[ -n ${SUN_EXECUTABLE:-} ]]; then
  "$SUN_EXECUTABLE" --version
  exit 0
fi
if command -v sun >/dev/null && [[ ${1:-} != --update ]]; then
  sun --version
  exit 0
fi
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
if [[ $(id -u) == 0 ]]; then
  apt-get update
  apt-get install -y "$package_dir/sun.deb"
else
  sudo apt-get update
  sudo apt-get install -y "$package_dir/sun.deb"
fi
sun --version
