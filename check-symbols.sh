#!/usr/bin/env bash
# Verify every undefined symbol in a Hyprland plugin resolves against the
# Hyprland binary plus its shared-library closure.
#
# A clean compile does NOT guarantee this: the plugin compiles against headers,
# but resolves against the installed binary. A mismatch is not a load error --
# the dynamic linker kills the whole compositor with "symbol lookup error".
# That is exactly what took down the session on 2026-07-28.
#
# Usage: ./check-symbols.sh build/hypr-hot-edge.so
set -euo pipefail

SO=${1:?usage: check-symbols.sh <plugin.so>}
HYPRLAND=${HYPRLAND:-/usr/bin/Hyprland}

# Everything the compositor can supply: its own exports + every lib it and the
# plugin pull in. ldd output lines look like "  libfoo.so => /path/libfoo.so (0x...)".
provided=$(mktemp)
trap 'rm -f "$provided" "$needed"' EXIT

{
  nm -D --defined-only "$HYPRLAND" 2>/dev/null || true
  for lib in $(ldd "$HYPRLAND" "$SO" 2>/dev/null | awk '$2=="=>" && $3 ~ /^\// {print $3}' | sort -u); do
    nm -D --defined-only "$lib" 2>/dev/null || true
  done
} | awk '{print $NF}' | sed 's/@.*//' | sort -u > "$provided"

# Strip @VERSION: nm writes "sym@GLIBC_2.2.5" for undefined but "sym@@GLIBC_2.2.5"
# for the defining lib, so unnormalized names never compare equal. Hyprland's own
# C++ symbols are unversioned, which is what we actually care about here.
# Only strong undefined ("U"); weak ones ("w", e.g. __gmon_start__, _ITM_*) are
# allowed to resolve to zero and are not a load failure.
needed=$(mktemp)
nm -D --undefined-only "$SO" 2>/dev/null | awk '$1=="U"{print $2}' | sed 's/@.*//' | sort -u > "$needed"

missing=$(comm -23 "$needed" "$provided")

if [[ -n $missing ]]; then
  echo "UNRESOLVED against $HYPRLAND:"
  while read -r sym; do
    [[ -z $sym ]] && continue
    printf '  %s\n      %s\n' "$sym" "$(c++filt <<<"$sym")"
  done <<<"$missing"
  exit 1
fi

echo "OK: all $(wc -l < "$needed") undefined symbols in $(basename "$SO") resolve."
