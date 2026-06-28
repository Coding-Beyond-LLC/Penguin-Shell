#!/usr/bin/env bash
set -euo pipefail

SRC="/home/nateblanquel/Penguin-Shell"
BUILD="/home/nateblanquel/Penguin-Shell/build"
BIN=penguin
DEST="~/.local/bin"
PEERS=(blade2 blade3 blade4)

cmake -S "$SRC" -B "$BUILD"
cmake --build "$BUILD"

mkdir -p "$DEST"
install -m 755 "$BUILD/$BIN" "$DEST/$BIN"
echo "nateblanquel@blade1: OK"

for s in "${PEERS[@]}"; do
    (
        ssh "nateblanquel@$s" 'mkdir -p ~/.local/bin' \
        && scp -q "$BUILD/$BIN" "nateblanquel@$s:~/.local/bin/$BIN" \
        && ssh "nateblanquel@$s" "chmod 755 ~/.local/bin/$BIN" \
        && echo "nateblanquel@$s: OK" || echo "nateblanquel@$s: FAILED"
    )
done
wait
