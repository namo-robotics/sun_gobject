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
# Hash file contents so timestamp-only changes do not regenerate bindings.
if command -v sha256sum >/dev/null; then
  hash_command=(sha256sum)
else
  hash_command=(shasum -a 256)
fi

# Include every GIR search directory, including added or removed metadata files.
gir_directories=(/usr/share/gir-1.0)
if [[ $target == x86_64-linux-gnu ]]; then
  gir_directories+=(/usr/lib/x86_64-linux-gnu/gir-1.0)
fi
for ((i = 0; i < ${#arguments[@]}; i++)); do
  if [[ ${arguments[i]} == --gir-dir ]]; then
    gir_directories+=("${arguments[i + 1]}")
  fi
done

# Fingerprint the generator, recipe, options, selection, overrides, and GIR inputs.
generation_inputs() {
  printf '%s\n' "${arguments[@]}"
  "${hash_command[@]}" "${BASH_SOURCE[0]}" "$SUN_BUILD/host/sun_gir" \
    src/bindings/selection.xml src/bindings/overrides.xml || return
  for directory in "${gir_directories[@]}"; do
    if [[ -d $directory ]]; then
      find -L "$directory" -type f -name '*.gir' -exec "${hash_command[@]}" {} + | LC_ALL=C sort || return
    fi
  done
}

# Verify generated files too, so deleting or editing an output triggers repair.
generation_outputs() {
  find "$SUN_BUILD/generated" -type f -exec "${hash_command[@]}" {} + | LC_ALL=C sort
}

stamp="$SUN_BUILD/host/gir-generation.sha256"
inputs=$(generation_inputs | "${hash_command[@]}")
outputs=$(generation_outputs | "${hash_command[@]}")
if [[ -f $stamp && $(cat "$stamp") == "$inputs"$'\n'"$outputs" ]]; then
  echo "[sun][info]: Up to date: GIR bindings"
else
  "$SUN_BUILD/host/sun_gir" "${arguments[@]}"
  outputs=$(generation_outputs | "${hash_command[@]}")
  printf '%s\n%s\n' "$inputs" "$outputs" > "$stamp"
fi

exec "$SUN_EXECUTABLE" build-compile
