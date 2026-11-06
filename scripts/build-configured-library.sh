#!/usr/bin/env bash
# Generate bindings in the configured build directory and compile the core library.
set -euo pipefail

: "${SUN_EXECUTABLE:?Run this script through sun build --config sun-config.json}"
: "${SUN_BUILD:?Missing configured build directory}"
: "${SUN_TARGET:?Missing configured target}"

sources="$SUN_BUILD/generated/sources"
mkdir -p "$SUN_BUILD/host" "$sources"
flags=()
read -r -a flags <<< "$(pkg-config --libs glib-2.0)"
# The generator executes on the host; only its emitted declarations use SUN_TARGET.
env -u SUN_BUILD_CONTEXT "$SUN_EXECUTABLE" --dynamic -c "${flags[@]}" \
  --no-test -o "$SUN_BUILD/host/sun_gir" src/generator/main.sun

case "$SUN_TARGET" in
  arm64-*darwin*|aarch64-*darwin*) target=arm64-apple-darwin ;;
  x86_64-*linux*) target=x86_64-linux-gnu ;;
  *) echo "Unsupported GIR binding target: $SUN_TARGET" >&2; exit 1 ;;
esac
arguments=(--selection src/bindings/selection.xml
  --overrides src/bindings/overrides.xml
  --output-dir "$SUN_BUILD/generated" --source-dir "$sources"
  --target "$target")
if [[ $(uname -s) == Darwin ]]; then
  brew_prefix=$(brew --prefix)
  gtk_prefix=${SUN_GTK_PREFIX:-$(pkg-config --variable=prefix gtk4)}
  arguments+=(--gir-dir "$gtk_prefix/share/gir-1.0")
  for directory in "$brew_prefix"/opt/*/share/gir-1.0; do
    if [[ -d $directory ]]; then arguments+=(--gir-dir "$directory"); fi
  done
fi
"$SUN_BUILD/host/sun_gir" "${arguments[@]}"

exec "$SUN_EXECUTABLE" build-compile
