#!/usr/bin/env python3
"""
screenshot_diff.py — Visual diff and analysis for Chlorlite screenshots.
Used by Claude to detect rendering issues, compare frames, describe what's visible.

Usage:
  python3 screenshot_diff.py describe <image.png>
  python3 screenshot_diff.py diff <a.png> <b.png>
  python3 screenshot_diff.py stats <image.png>
  python3 screenshot_diff.py grid <dir/> [n_frames]
"""

import sys, os, struct, zlib, math

def read_png_raw(path):
    """Read PNG without PIL — returns (width, height, rgba_bytes)"""
    with open(path, 'rb') as f:
        sig = f.read(8)
        assert sig == b'\x89PNG\r\n\x1a\n', "Not a PNG"
        w = h = 0
        idat = b''
        while True:
            length_bytes = f.read(4)
            if len(length_bytes) < 4: break
            length = struct.unpack('>I', length_bytes)[0]
            chunk_type = f.read(4).decode('ascii', errors='replace')
            data = f.read(length)
            f.read(4)  # CRC
            if chunk_type == 'IHDR':
                w, h = struct.unpack('>II', data[:8])
            elif chunk_type == 'IDAT':
                idat += data
            elif chunk_type == 'IEND':
                break
    # Decompress
    raw = zlib.decompress(idat)
    # Reconstruct scanlines (filter byte per row)
    stride = w * 4 + 1
    pixels = bytearray(w * h * 4)
    prev = bytearray(w * 4)
    for y in range(h):
        row_start = y * stride
        filt = raw[row_start]
        row = bytearray(raw[row_start+1 : row_start+stride])
        if filt == 0:   # None
            out = row
        elif filt == 1: # Sub
            out = bytearray(len(row))
            for i, b in enumerate(row):
                a = out[i-4] if i >= 4 else 0
                out[i] = (b + a) & 0xff
        elif filt == 2: # Up
            out = bytearray((b + p) & 0xff for b, p in zip(row, prev))
        elif filt == 3: # Average
            out = bytearray(len(row))
            for i, b in enumerate(row):
                a = out[i-4] if i >= 4 else 0
                p = prev[i]
                out[i] = (b + (a + p) // 2) & 0xff
        elif filt == 4: # Paeth
            out = bytearray(len(row))
            def paeth(a,b,c):
                p = a+b-c; pa=abs(p-a); pb=abs(p-b); pc=abs(p-c)
                return a if pa<=pb and pa<=pc else (b if pb<=pc else c)
            for i, b in enumerate(row):
                a = out[i-4] if i >= 4 else 0
                pp = prev[i]; pc = prev[i-4] if i >= 4 else 0
                out[i] = (b + paeth(a,pp,pc)) & 0xff
        else:
            out = row
        pixels[y*w*4:(y+1)*w*4] = out
        prev = out
    return w, h, bytes(pixels)

def describe(path):
    try:
        w, h, px = read_png_raw(path)
    except Exception as e:
        print(f"Error reading {path}: {e}"); return

    total = w * h
    r_sum = g_sum = b_sum = 0
    black = white = 0
    r_hist = [0]*8; g_hist = [0]*8; b_hist = [0]*8

    for i in range(0, len(px), 4):
        r,g,b,a = px[i],px[i+1],px[i+2],px[i+3]
        r_sum+=r; g_sum+=g; b_sum+=b
        r_hist[r>>5]+=1; g_hist[g>>5]+=1; b_hist[b>>5]+=1
        if r<8 and g<8 and b<8: black+=1
        if r>248 and g>248 and b>248: white+=1

    r_avg = r_sum/total; g_avg = g_sum/total; b_avg = b_sum/total
    brightness = (r_avg+g_avg+b_avg)/3

    print(f"Image: {os.path.basename(path)}")
    print(f"  Size: {w}x{h} ({total:,} pixels)")
    print(f"  Avg color: R={r_avg:.0f} G={g_avg:.0f} B={b_avg:.0f}")
    print(f"  Brightness: {brightness:.1f}/255 ({brightness/2.55:.0f}%)")
    print(f"  Black pixels:  {black:,} ({100*black/total:.1f}%)")
    print(f"  White pixels:  {white:,} ({100*white/total:.1f}%)")
    print(f"  Colored (non-grey) pixels: ~{100*(1 - black/total - white/total):.1f}%")

    # Dominant color guess
    dominant = ""
    if brightness < 10: dominant = "BLACK FRAME — renderer may not be outputting anything"
    elif brightness > 245: dominant = "WHITE FRAME — possible clear color issue"
    elif r_avg > g_avg*1.5 and r_avg > b_avg*1.5: dominant = "Predominantly RED"
    elif g_avg > r_avg*1.5 and g_avg > b_avg*1.5: dominant = "Predominantly GREEN"
    elif b_avg > r_avg*1.5 and b_avg > g_avg*1.5: dominant = "Predominantly BLUE"
    elif abs(r_avg-g_avg)<10 and abs(g_avg-b_avg)<10: dominant = "Grey/neutral"
    else: dominant = "Mixed colors (scene likely rendering)"

    print(f"  Assessment: {dominant}")

def diff(path_a, path_b):
    wa,ha,pa = read_png_raw(path_a)
    wb,hb,pb = read_png_raw(path_b)
    if wa!=wb or ha!=hb:
        print(f"Size mismatch: {wa}x{ha} vs {wb}x{hb}"); return
    total = wa*ha
    changed = 0; max_diff = 0; total_diff = 0
    for i in range(0, len(pa), 4):
        dr = abs(int(pa[i])-int(pb[i]))
        dg = abs(int(pa[i+1])-int(pb[i+1]))
        db = abs(int(pa[i+2])-int(pb[i+2]))
        d = max(dr,dg,db)
        if d > 4: changed += 1
        total_diff += d; max_diff = max(max_diff, d)
    avg_diff = total_diff / total
    print(f"Frame diff: {os.path.basename(path_a)} vs {os.path.basename(path_b)}")
    print(f"  Changed pixels: {changed:,} / {total:,} ({100*changed/total:.2f}%)")
    print(f"  Max channel diff: {max_diff}")
    print(f"  Avg channel diff: {avg_diff:.2f}")
    if changed == 0:       print("  → IDENTICAL frames")
    elif changed < 100:    print("  → Nearly identical (minor noise/antialiasing)")
    elif changed < total*0.01: print("  → Minor changes (particle effects, UI updates)")
    elif changed < total*0.2:  print("  → Moderate changes (animation, movement)")
    else:                      print("  → Major changes (scene transition or heavy motion)")

def grid_summary(directory, n=6):
    pngs = sorted(f for f in os.listdir(directory) if f.endswith('.png'))[:n]
    if not pngs:
        print(f"No PNGs in {directory}"); return
    print(f"Frame summary — {len(pngs)} frames from {directory}")
    for name in pngs:
        try:
            w,h,px = read_png_raw(os.path.join(directory,name))
            brightness = sum(px[i::4] for i in range(3)) / (3*w*h) if False else \
                         sum(px[i]+px[i+1]+px[i+2] for i in range(0,len(px),4))/(3*w*h)
            print(f"  {name}: {w}x{h}  brightness={brightness:.0f}/255")
        except Exception as e:
            print(f"  {name}: error ({e})")

if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'help'
    if cmd == 'describe' and len(sys.argv) > 2:
        describe(sys.argv[2])
    elif cmd == 'diff' and len(sys.argv) > 3:
        diff(sys.argv[2], sys.argv[3])
    elif cmd == 'grid' and len(sys.argv) > 2:
        n = int(sys.argv[3]) if len(sys.argv) > 3 else 6
        grid_summary(sys.argv[2], n)
    elif cmd == 'stats' and len(sys.argv) > 2:
        describe(sys.argv[2])
    else:
        print(__doc__)
