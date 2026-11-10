#!/usr/bin/env bash
# Prepara o motor: copia (ou baixa) o REDRIVER2 no commit fixado para port/redriver2. Rode UMA vez.
#   bash setup_engine.sh                    -> usa ../ReDriver 2 se existir (clone local), senao baixa do GitHub
#   bash setup_engine.sh --force            -> refaz do zero
#   bash setup_engine.sh "<pasta do clone>" -> usa outra pasta local
set -euo pipefail
cd "$(dirname "$0")"
UPSTREAM="https://github.com/OpenDriver2/REDRIVER2.git"
PIN="b2d8857426f815007486def9bd23f9a6007a2c64"
PSYX_UP="https://github.com/OpenDriver2/PsyCross.git"
PSYX_PIN="e56e4cde1c2b8a15e0d4e38b26cdd9202e0d17e6"
DEST="port/redriver2"
FORCE=0; LOCAL=""
for a in "$@"; do if [ "$a" = "--force" ]; then FORCE=1; else LOCAL="$a"; fi; done
if [ -e "$DEST/src_rebuild/Game" ]; then
  if [ $FORCE = 1 ]; then rm -rf "$DEST"; else echo "$DEST ja existe. Para refazer: bash setup_engine.sh --force"; exit 0; fi
fi
[ -z "$LOCAL" ] && [ -d "../ReDriver 2/src_rebuild/Game" ] && LOCAL="../ReDriver 2"
mkdir -p "$DEST/src_rebuild"
if [ -n "$LOCAL" ]; then
  echo "Copiando de: $LOCAL"
  for d in Game utils; do cp -r "$LOCAL/src_rebuild/$d" "$DEST/src_rebuild/$d"; done
  mkdir -p "$DEST/src_rebuild/PsyCross"
  cp -r "$LOCAL/src_rebuild/PsyCross/include" "$LOCAL/src_rebuild/PsyCross/src" "$DEST/src_rebuild/PsyCross/"
  if [ -d "$LOCAL/data/DRIVER2/LANG" ]; then
    mkdir -p "$DEST/data/DRIVER2"
    cp -r "$LOCAL/data/DRIVER2/LANG" "$DEST/data/DRIVER2/"
  fi
else
  command -v git >/dev/null || { echo "ERRO: instale git"; exit 1; }
  T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
  git -C "$T" init -q; git -C "$T" remote add origin "$UPSTREAM"; git -C "$T" fetch -q --depth 1 origin "$PIN"; git -C "$T" checkout -q FETCH_HEAD
  mkdir -p "$T/psx"; git -C "$T/psx" init -q; git -C "$T/psx" remote add origin "$PSYX_UP"; git -C "$T/psx" fetch -q --depth 1 origin "$PSYX_PIN"; git -C "$T/psx" checkout -q FETCH_HEAD
  for d in Game utils; do cp -r "$T/src_rebuild/$d" "$DEST/src_rebuild/$d"; done
  mkdir -p "$DEST/src_rebuild/PsyCross"; cp -r "$T/psx/include" "$T/psx/src" "$DEST/src_rebuild/PsyCross/"
  if [ -d "$T/data/DRIVER2/LANG" ]; then
    mkdir -p "$DEST/data/DRIVER2"
    cp -r "$T/data/DRIVER2/LANG" "$DEST/data/DRIVER2/"
  fi
fi
bash ./patch_engine.sh
echo "Motor pronto em $DEST. Proximo passo: bash build.sh"
