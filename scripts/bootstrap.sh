#!/usr/bin/env bash
# Chlorlite bootstrap — compiles the engine in the current sandbox.
# Run this once per Claude session. Output: engine/build/libclaudecore.a + cc-sandbox
set -e

SKILL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="$SKILL_DIR/engine"
BUILD="$ENGINE/build"

echo "╔═ Chlorlite Bootstrap ════════════════════════════╗"
echo "║ Compiling engine in sandbox...                   ║"

# Install deps (no-op if already present). Refresh the package index first so
# installs work on a fresh sandbox with a stale/empty index.
if command -v apt-get &>/dev/null; then
    apt-get update -qq 2>/dev/null || true
    apt-get install -y -qq \
        cmake build-essential \
        libgl-dev libglfw3-dev libx11-dev \
        libosmesa6-dev \
        libopenal-dev libsndfile1-dev \
        libzstd-dev \
        python3-dev \
        2>/dev/null | grep -v "^$\|already" || true
fi

# Verify the essential libraries are actually present before configuring — fail
# with a clear message rather than a cryptic cmake error if a dep is missing.
_missing=""
for hdr in /usr/include/GL/osmesa.h /usr/include/GLFW/glfw3.h /usr/include/zstd.h; do
    [ -f "$hdr" ] || _missing="$_missing $hdr"
done
if [ -n "$_missing" ]; then
    echo "ERROR: required development headers are missing:$_missing" >&2
    echo "  Install them with: apt-get install libosmesa6-dev libglfw3-dev libzstd-dev libopenal-dev libsndfile1-dev" >&2
    echo "  (Audio deps optional; OSMesa+GLFW+zstd are required to build.)" >&2
    exit 1
fi

mkdir -p "$BUILD"
cd "$BUILD"
cmake "$ENGINE" \
    -DCMAKE_BUILD_TYPE=Release \
    -Wno-dev \
    -DCMAKE_C_FLAGS="-O3 -ffast-math" \
    2>&1 | grep -E "Chlorlite|error|FOUND" || true

make -j"$(nproc)" 2>&1 | grep -E "Built target|error:|warning:" | grep -v warning || true

echo "║"
echo "║ Engine compiled:"
ls -lh "$BUILD/libclaudecore.a" "$BUILD/cc-sandbox" 2>/dev/null | awk '{print "║  " $9 "  " $5}'
echo "╚═══════════════════════════════════════════════════╝"
echo ""
echo "Sandbox tool: $BUILD/cc-sandbox"
echo "Static lib:   $BUILD/libclaudecore.a"
echo "Headers:      $ENGINE/include/cc/"
echo ""
echo "To compile a game:"
echo "  bash $SKILL_DIR/scripts/build_game.sh /path/to/game"
echo "To run headless:"
echo "  bash $SKILL_DIR/scripts/run_headless.sh /path/to/game.so"
