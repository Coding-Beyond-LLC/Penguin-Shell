#!/usr/bin/env bash
set -euo pipefail

SRC="$HOME/Penguin-Shell"
BUILD="$HOME/Penguin-Shell/build"
BIN=penguin
DEST="$HOME/.local/bin"
PEERS=(slice2 slice3 slice4)

cmake -S "$SRC" -B "$BUILD"
cmake --build "$BUILD"

mkdir -p "$DEST"
install -m 755 "$BUILD/$BIN" "$DEST/$BIN"
chsh -s "$BUILD/$BIN"
echo "slice1: build OK"

for s in "${PEERS[@]}"; do
    (
        ssh "nateblanquel@$s" 'mkdir -p ~/.local/bin' \
        && scp -q "$BUILD/$BIN" "nateblanquel@$s:.local/bin/$BIN" \
        && ssh "nateblanquel@$s" "chmod 755 ~./local/bin/$BIN" \
        && ssh "nateblanquel@$s" "chsh -s "~./local/bin/$BIN" \
        && echo "nateblanquel@$s: OK" || echo "nateblanquel@$s: FAILED"
    )
done
wait