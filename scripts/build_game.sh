#!/usr/bin/env bash
# Build a game project against ClaudeCore.
# Usage: build_game.sh <game_dir_or_file> [output_name]
set -e

SKILL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="$SKILL_DIR/engine"
BUILD="$ENGINE/build"
LIB="$BUILD/libclaudecore.a"

if [ ! -f "$LIB" ]; then
    echo "[ClaudeCore] Engine not built yet. Running bootstrap..."
    bash "$SKILL_DIR/scripts/bootstrap.sh"
fi

GAME_PATH="${1:?Usage: build_game.sh <game.c or game_dir>}"
OUT="${2:-game}"

# Single file or directory?
if [ -f "$GAME_PATH" ]; then
    SOURCES="$GAME_PATH"
elif [ -d "$GAME_PATH" ]; then
    SOURCES=$(find "$GAME_PATH" -name "*.c" | tr '\n' ' ')
else
    echo "Error: '$GAME_PATH' not found"; exit 1
fi

# Collect link flags
LINK_FLAGS="-lpthread -lm"
pkg-config --libs osmesa 2>/dev/null && LINK_FLAGS="$LINK_FLAGS $(pkg-config --libs osmesa)"
pkg-config --libs gl 2>/dev/null     && LINK_FLAGS="$LINK_FLAGS $(pkg-config --libs gl)"
pkg-config --libs glfw3 2>/dev/null  && LINK_FLAGS="$LINK_FLAGS $(pkg-config --libs glfw3)"
pkg-config --libs openal 2>/dev/null && LINK_FLAGS="$LINK_FLAGS $(pkg-config --libs openal)"
pkg-config --libs sndfile 2>/dev/null&& LINK_FLAGS="$LINK_FLAGS $(pkg-config --libs sndfile)"
[ -f /usr/lib/x86_64-linux-gnu/libX11.so ] && LINK_FLAGS="$LINK_FLAGS -lX11"

gcc -O2 -std=c17 \
    -I"$ENGINE/include" \
    -I"$ENGINE/qwerty/include" \
    $SOURCES \
    "$LIB" \
    "$BUILD/qwerty/libqwerty.a" \
    $LINK_FLAGS \
    -o "$OUT"

echo "[ClaudeCore] Built: $OUT"
