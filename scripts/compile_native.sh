#!/usr/bin/env bash
# compile_native.sh — Compile a CC game into a STANDALONE native executable
# that a normal end-user can run with zero CC/skill/toolchain installed.
#
# The produced binary:
#   - statically links the CC engine (libclaudecore.a + libqwerty.a) — no CC install needed
#   - dynamically links only standard desktop libraries (GLFW, OpenGL, X11, and
#     optionally OpenAL/sndfile for audio, zstd for model loading)
#   - opens a real hardware-accelerated window via GLFW
#
# Usage: compile_native.sh <game.c|game_dir> [output_name]
#
# Result: ./<output_name> — a single ELF executable. Ship it. Double-click it.
set -e

SKILL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="$SKILL_DIR/engine"
NBUILD="$ENGINE/build-native"   # separate windowed-only build

GAME_PATH="${1:?Usage: compile_native.sh <game.c or game_dir> [output]}"
OUT="${2:-game}"

echo "╔═ CC Native Compile ═══════════════════════════════╗"

# 1. Ensure deps (windowed desktop stack + optional audio/model)
if command -v apt-get &>/dev/null; then
    apt-get update -qq 2>/dev/null || true
    apt-get install -y -qq cmake build-essential libglfw3-dev libgl-dev \
        libx11-dev libopenal-dev libsndfile1-dev libzstd-dev 2>/dev/null | grep -v "^$\|already" || true
fi

# 2. Build a WINDOWED-ONLY engine (no OSMesa → no headless-only dependency)
if [ ! -f "$NBUILD/libclaudecore.a" ]; then
    echo "║ Building windowed engine (one-time)...            ║"
    mkdir -p "$NBUILD"; cd "$NBUILD"
    cmake "$ENGINE" -DCMAKE_BUILD_TYPE=Release -Wno-dev \
        -DCC_WINDOWED=ON -DCC_HEADLESS=OFF \
        -DCMAKE_C_FLAGS="-O3" >/dev/null 2>&1
    make -j"$(nproc)" claudecore qwerty_static >/dev/null 2>&1
fi

# 3. Gather sources
if [ -f "$GAME_PATH" ]; then SOURCES="$GAME_PATH"
elif [ -d "$GAME_PATH" ]; then SOURCES=$(find "$GAME_PATH" -name "*.c" | tr '\n' ' ')
else echo "Error: '$GAME_PATH' not found"; exit 1; fi

# 4. Link flags: static engine, dynamic standard desktop libs
LINK="-lglfw -lGL -lX11 -lm -lpthread -ldl"
command -v pkg-config >/dev/null && {
    pkg-config --exists openal  && LINK="$LINK $(pkg-config --libs openal)"
    pkg-config --exists sndfile && LINK="$LINK $(pkg-config --libs sndfile)"
}
[ -f /usr/lib/x86_64-linux-gnu/libzstd.so ] && LINK="$LINK -lzstd"

# 5. Compile the standalone executable
gcc -O2 -std=c17 \
    -I"$ENGINE/include" -I"$ENGINE/qwerty/include" \
    $SOURCES \
    "$NBUILD/libclaudecore.a" "$NBUILD/qwerty/libqwerty.a" \
    $LINK \
    -o "$OUT"

echo "║ Built standalone executable: $OUT"
echo "╚═══════════════════════════════════════════════════╝"
echo ""
echo "Runtime deps (standard on any Linux desktop):"
ldd "$OUT" 2>/dev/null | grep -iE "glfw|libGL|X11|openal|sndfile|zstd" | sed 's/^/  /' || true
echo ""
echo "Ship '$OUT' to any Linux user — no CC, no skill, no toolchain required."
