#!/usr/bin/env bash
# Re-render the "pet me" clip with SAM and regenerate firmware/desktop_pet/pet_me.h.
# Requires: git, a C compiler (gcc/clang), python3. Run from anywhere.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

PHONEMES="${PHONEMES:-P/HEH5T, MIY4}"   # SAM phonetic alphabet; stress digits 4-6 lengthen vowels
PARAMS="${PARAMS:-80 64 128 128}"       # speed pitch throat mouth

git clone -q --depth 1 https://github.com/earlephilhower/ESP8266SAM.git "$WORK/sam"
S="$WORK/sam/src"
cc -O2 -w -I"$HERE/shim" -I"$S" -o "$WORK/sam_render" "$HERE/sam_render.c" \
   "$S/sam.c" "$S/render.c" "$S/reciter.c" "$S/esp8266sam_debug.c"
# shellcheck disable=SC2086
"$WORK/sam_render" "$PHONEMES" $PARAMS "$WORK/pet_me.raw" p
python3 "$HERE/make_header.py" "$WORK/pet_me.raw" "$ROOT/firmware/desktop_pet/pet_me.h" \
        "$ROOT/docs/media/pet_me.wav" "$PHONEMES" "$PARAMS"
