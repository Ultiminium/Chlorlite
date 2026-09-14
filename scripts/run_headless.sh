#!/usr/bin/env bash
# Run a game binary or .so headlessly with screenshots for Claude to inspect.
# Usage: run_headless.sh <binary_or_so> [--ticks N] [--screenshot N] [--fps N]

SKILL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SANDBOX="$SKILL_DIR/engine/build/cc-sandbox"

if [ ! -f "$SANDBOX" ]; then
    echo "[Chlorlite] Engine not built. Running bootstrap..."
    bash "$SKILL_DIR/scripts/bootstrap.sh"
fi

TARGET="${1:?Usage: run_headless.sh <game> [--ticks N] [--screenshot N]}"
shift

TICKS=120
SCREENSHOT=30
FPS=60
SS_DIR="/tmp/cc_screenshots"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ticks)      TICKS="$2";      shift 2;;
        --screenshot) SCREENSHOT="$2"; shift 2;;
        --fps)        FPS="$2";        shift 2;;
        --outdir)     SS_DIR="$2";     shift 2;;
        *) shift;;
    esac
done

mkdir -p "$SS_DIR"

if [[ "$TARGET" == *.so ]]; then
    "$SANDBOX" "$TARGET" --ticks "$TICKS" --screenshot "$SCREENSHOT" --fps "$FPS"
else
    # It's a standalone binary — run it with headless env vars
    DISPLAY="" CC_HEADLESS=1 CC_SCREENSHOT_DIR="$SS_DIR" \
        "$TARGET" --headless --ticks "$TICKS" --screenshot "$SCREENSHOT"
fi

echo ""
echo "[Chlorlite] Screenshots saved to: $SS_DIR"
echo "Files:"
ls -1 "$SS_DIR"/*.png 2>/dev/null | tail -10
