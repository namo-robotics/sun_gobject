#!/usr/bin/env bash
# Build configured Sun packages and publish target-qualified assets.
set -euo pipefail
cd "$(dirname "$0")/.."
compiler=${SUN_EXECUTABLE:-sun}
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)
    target=x86_64-linux-gnu
    compiler_target=x86_64-pc-linux-gnu
    ;;
  Darwin-arm64)
    target=arm64-apple-darwin
    compiler_target=arm64-apple-darwin
    ;;
  *) echo 'Unsupported release target' >&2; exit 1 ;;
esac

# Match the target asserted by generated C bindings; asset names use shorter labels.
"$compiler" build --config sun-config.json --target "$compiler_target" --dynamic
build_root=$("$compiler" describe --config sun-config.json --target "$compiler_target" --dynamic --field build_root)
package_dir="$build_root/packages/sun_gobject"
mkdir -p build/release
cp "$package_dir.tar.gz" "build/release/sun_gobject-$target.sun.tar.gz"
for name in sun_gobject sun_gtk; do
  cp "$package_dir/lib/$name.moon" "build/release/$name-$target.moon"
done
if [[ $target == x86_64-linux-gnu ]]; then
  cp "$package_dir/lib/sun_capture.moon" "build/release/sun_capture-$target.moon"
fi
