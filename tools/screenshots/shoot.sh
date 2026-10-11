#!/usr/bin/env bash
# README images: every page in the light and the dark theme, rendered offscreen by HuldraRenderTest and compressed
# to WebP (cwebp), then the product image on top of the README (hero.mjs, Playwright).
# Usage: tools/screenshots/shoot.sh      (first time: cd tools/screenshots && npm install)
set -euo pipefail
cd "$(dirname "$0")/../.."
TOOL=build/HuldraRenderTest_artefacts/Release/HuldraRenderTest
OUT=docs/screenshots
RAW=build/screenshots/raw
mkdir -p "$OUT" "$RAW"

# id  page  preset[:param=value,...]  [fx index]
SHOTS=(
  "source|source|Paper Moon|"
  "slice|slice|Hookline Chops|"
  "keymap|keymap|Polished Ooh:test_samples=4|"
  "engine|engine|Aurora Choir|"
  "vocal|engine|Close To You|"
  "spectral|spectral|Frozen Lake:engine=2|"
  "keys|keys|Sad Stack:test_keys=1,scl_on=1,scl_root=9,scl_type=1|"
  "mod|mod|Daydream Drift|"
  "motion|motion|Ladder Slices|"
  "stars|motion|Glass Constellation|"
  "fx|fx|Halo Ballad|7"
  "browser|browser|Paper Moon|"
)

for shot in "${SHOTS[@]}"; do
  IFS='|' read -r id page preset fx <<< "$shot"
  for theme in light dark; do
    "$TOOL" --screenshot "$page" "$preset" "$theme" "$RAW/$id-$theme.png" ${fx:+"$fx"}
    cwebp -quiet -q 86 -resize 1650 0 "$RAW/$id-$theme.png" -o "$OUT/$id-$theme.webp"
  done
  echo "shot  $id"
done

node tools/screenshots/hero.mjs
