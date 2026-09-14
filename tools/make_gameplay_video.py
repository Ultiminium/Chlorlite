#!/usr/bin/env python3
"""make_gameplay_video.py — assemble a captured frame sequence into watchable output.

Turns tetris_demo's frame_%06d.png sequence into:
  1. an MP4 (real-time playback, for a human to watch)
  2. a GIF (inline-viewable loop)
  3. a CONTACT SHEET grid (many frames in one image, so gameplay MOTION can be
     inspected at a glance frame-accurately — this is how Claude "watches" it)

Usage: make_gameplay_video.py <frame_dir> <out_prefix> [--fps N] [--sheet-cols N] [--sheet-step N]
"""
import sys, os, glob, subprocess, argparse

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frame_dir")
    ap.add_argument("out_prefix")
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--sheet-cols", type=int, default=8)
    ap.add_argument("--sheet-step", type=int, default=10,
                    help="put every Nth frame in the contact sheet")
    a = ap.parse_args()

    frames = sorted(glob.glob(os.path.join(a.frame_dir, "frame_*.png")))
    if not frames:
        print("no frames found in", a.frame_dir); return 1
    print(f"{len(frames)} frames")

    # 1. MP4 (H.264, widely playable)
    mp4 = a.out_prefix + ".mp4"
    subprocess.run([
        "ffmpeg", "-y", "-framerate", str(a.fps),
        "-i", os.path.join(a.frame_dir, "frame_%06d.png"),
        "-c:v", "libx264", "-pix_fmt", "yuv420p",
        "-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2", mp4
    ], check=True, capture_output=True)
    print("wrote", mp4)

    # 2. GIF (looping, smaller for inline viewing)
    gif = a.out_prefix + ".gif"
    subprocess.run([
        "ffmpeg", "-y", "-framerate", str(a.fps),
        "-i", os.path.join(a.frame_dir, "frame_%06d.png"),
        "-vf", "fps=20,scale=320:-1:flags=lanczos", gif
    ], check=True, capture_output=True)
    print("wrote", gif)

    # 3. CONTACT SHEET — every Nth frame tiled into a grid, labeled with frame #.
    #    This is the key artifact for frame-accurate inspection of motion.
    from PIL import Image, ImageDraw
    picks = frames[::a.sheet_step]
    thumbs = []
    for f in picks:
        im = Image.open(f).convert("RGB")
        im.thumbnail((260, 260))
        d = ImageDraw.Draw(im)
        n = os.path.basename(f).replace("frame_", "").replace(".png", "").lstrip("0") or "0"
        d.rectangle([0, 0, 42, 14], fill=(0, 0, 0))
        d.text((3, 2), "f" + n, fill=(255, 255, 0))
        thumbs.append(im)
    if thumbs:
        cols = a.sheet_cols
        rows = (len(thumbs) + cols - 1) // cols
        tw, th = thumbs[0].size
        sheet = Image.new("RGB", (cols * tw, rows * th), (20, 20, 26))
        for i, t in enumerate(thumbs):
            sheet.paste(t, ((i % cols) * tw, (i // cols) * th))
        sheet_path = a.out_prefix + "_contact_sheet.png"
        sheet.save(sheet_path)
        print("wrote", sheet_path, f"({len(thumbs)} frames, every {a.sheet_step})")
    return 0

if __name__ == "__main__":
    sys.exit(main())
