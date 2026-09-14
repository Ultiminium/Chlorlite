#!/usr/bin/env python3
"""inspect.py — a MEASURED viewer. Turns a render into numbers you can't narrate away.

The failure this fixes: a session looked at a figure with flat ribbon arms and wrote
"smooth, round arms" — describing what it hoped for, not what it rendered. Handing
back a bare image lets that happen. This tool instead MEASURES the pixels and prints
the measurements ON the image, so "round arms" becomes "arm 7px wide at its thinnest
= a ribbon" — a claim the numbers contradict.

Usage:
  inspect.py <image.png>                       → measured report + annotated image
  inspect.py <before.png> <after.png> --diff   → what actually changed, side by side
  inspect.py <image.png> --figure              → humanoid-specific checks (limb widths,
                                                 head/body ratio, L/R symmetry, gaps)

Outputs an annotated PNG next to the input (<name>_inspected.png) and prints a report.
The report is deliberately blunt: it states FAIL/OK against thresholds so a caller
must confront a failing number instead of writing prose around it.
"""
import sys, os, argparse
from PIL import Image, ImageDraw, ImageFont

def load(p):
    im = Image.open(p).convert("RGBA")
    return im

def subject_mask(im, bg_thresh=24):
    """Boolean mask of non-background pixels. Background = near-black or transparent."""
    px = im.load(); w,h = im.size
    mask = [[False]*w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r,g,b,a = px[x,y]
            if a < 16:  # transparent
                continue
            if r+g+b > bg_thresh:  # not near-black background
                mask[y][x] = True
    return mask, w, h

def bbox(mask, w, h):
    xs0,ys0,xs1,ys1 = w,h,-1,-1
    for y in range(h):
        row = mask[y]
        for x in range(w):
            if row[x]:
                if x<xs0: xs0=x
                if x>xs1: xs1=x
                if y<ys0: ys0=y
                if y>ys1: ys1=y
    return (xs0,ys0,xs1,ys1) if xs1>=0 else None

def row_spans(mask, w, y):
    """Return list of (start,end) filled horizontal spans on row y — a limb slice."""
    spans=[]; s=None
    for x in range(w):
        if mask[y][x] and s is None: s=x
        elif not mask[y][x] and s is not None: spans.append((s,x-1)); s=None
    if s is not None: spans.append((s,w-1))
    return spans

def measure_figure(im):
    """Humanoid-oriented measurements. Returns a dict of numbers + verdicts."""
    mask,w,h = subject_mask(im)
    bb = bbox(mask,w,h)
    R={}
    if not bb:
        R['error']="no subject found (all background)"; return R
    x0,y0,x1,y1 = bb
    fig_w=x1-x0+1; fig_h=y1-y0+1
    R['bbox']=bb; R['figure_w']=fig_w; R['figure_h']=fig_h
    R['aspect']=round(fig_h/max(fig_w,1),2)

    # sample horizontal slices at fractions of the figure height (0=top/head, 1=feet)
    def width_at(frac):
        y=int(y0+frac*(fig_h-1))
        sp=row_spans(mask,w,y)
        if not sp: return 0,0,[]
        total=sum(e-s+1 for s,e in sp)
        widest=max(e-s+1 for s,e in sp)
        return total,widest,sp

    # head band ~ top 12%, find its widest row
    head_w=max(width_at(f)[1] for f in (0.02,0.05,0.08,0.11))
    # shoulders ~ 18-22%
    sh_total = max(width_at(f)[0] for f in (0.16,0.19,0.22))
    # arms: at shoulder height in T-pose, the arm tubes are the thin far-left/right spans.
    # measure the THINNEST span vertical-thickness by scanning a vertical line through
    # the outer 15% of the bbox width.
    def limb_thickness_at_x(xc):
        ys=[y for y in range(y0,y1+1) if 0<=xc<w and mask[y][xc]]
        if not ys: return 0
        # contiguous run containing the median
        ys.sort(); return ys[-1]-ys[0]+1
    left_x  = x0 + int(fig_w*0.06)
    right_x = x1 - int(fig_w*0.06)
    arm_th_l = limb_thickness_at_x(left_x)
    arm_th_r = limb_thickness_at_x(right_x)
    R['head_width_px']=head_w
    R['shoulder_width_px']=sh_total
    R['arm_thickness_l_px']=arm_th_l
    R['arm_thickness_r_px']=arm_th_r
    R['head_to_figure_ratio']=round(head_w/max(fig_w,1),3)

    # symmetry: compare filled-pixel count left vs right of the vertical centerline
    cx=(x0+x1)//2; ln=rn=0
    for y in range(y0,y1+1):
        for x in range(x0,x1+1):
            if mask[y][x]:
                if x<cx: ln+=1
                else: rn+=1
    sym = 1.0 - abs(ln-rn)/max(ln+rn,1)
    R['symmetry']=round(sym,3)

    # gaps/holes: count background pixels fully enclosed by subject on a mid band
    holes=0
    for y in range(y0+fig_h//4, y1-fig_h//4):
        sp=row_spans(mask,w,y)
        if len(sp)>=2:
            # background between the first and last span = internal gap (e.g. armpit/crotch void)
            holes += (sp[-1][0]-sp[0][1]-1)
    R['internal_gap_px']=holes

    # ---- VERDICTS (blunt thresholds) ----
    v=[]
    # ribbon-arm detection: an arm thinner than ~1/3 of head width is a flat ribbon
    thin = min(a for a in (arm_th_l,arm_th_r) if True)
    if head_w>0 and thin < head_w*0.35:
        v.append(("FAIL","arms","arm thickness %dpx < %.0f%% of head width (%dpx) → reads as a FLAT RIBBON, not a round limb"
                  %(thin, 35, head_w)))
    else:
        v.append(("OK","arms","arm thickness %dpx vs head %dpx — has real girth"%(thin,head_w)))
    # head ratio: humanoid head ~1/7 to 1/4 of body width is sane
    hr=R['head_to_figure_ratio']
    if hr < 0.12: v.append(("FAIL","head","head is %.0f%% of figure width — too small"%(hr*100)))
    elif hr > 0.45: v.append(("FAIL","head","head is %.0f%% of figure width — too large"%(hr*100)))
    else: v.append(("OK","head","head/figure ratio %.2f is in range"%hr))
    # symmetry
    if sym < 0.85: v.append(("FAIL","symmetry","L/R symmetry %.2f — figure is lopsided"%sym))
    else: v.append(("OK","symmetry","L/R symmetry %.2f"%sym))
    R['verdicts']=v
    return R

def annotate(im, R, out):
    im=im.convert("RGB"); d=ImageDraw.Draw(im); w,h=im.size
    if 'bbox' in R:
        x0,y0,x1,y1=R['bbox']; d.rectangle([x0,y0,x1,y1],outline=(0,200,255),width=1)
    # draw the report as a panel
    lines=[]
    if 'error' in R: lines=["ERROR: "+R['error']]
    else:
        lines=[ "figure %dx%d  aspect %.2f"%(R['figure_w'],R['figure_h'],R['aspect']),
                "head %dpx  shoulders %dpx"%(R['head_width_px'],R['shoulder_width_px']),
                "arm L/R %d/%d px  sym %.2f  gap %dpx"%(R['arm_thickness_l_px'],R['arm_thickness_r_px'],R['symmetry'],R['internal_gap_px']),
                "" ]
        for verdict,tag,msg in R.get('verdicts',[]):
            lines.append(("[%s] %s: %s"%(verdict,tag,msg))[:78])
    pad=6; lh=13; pw=min(w, 560); ph=pad*2+lh*len(lines)
    d.rectangle([0,0,pw,ph],fill=(0,0,0))
    for i,ln in enumerate(lines):
        col=(255,90,80) if ln.startswith("[FAIL") else (120,255,150) if ln.startswith("[OK") else (220,220,220)
        d.text((pad,pad+i*lh),ln,fill=col)
    im.save(out)
    return out

def print_report(name, R):
    print("── inspect: %s ──"%name)
    if 'error' in R: print("  ERROR:",R['error']); return
    print("  figure %dx%d  aspect %.2f  sym %.2f  gap %dpx"%(R['figure_w'],R['figure_h'],R['aspect'],R['symmetry'],R['internal_gap_px']))
    print("  head %dpx  shoulders %dpx  arms L/R %d/%d px"%(R['head_width_px'],R['shoulder_width_px'],R['arm_thickness_l_px'],R['arm_thickness_r_px']))
    nfail=0
    for verdict,tag,msg in R.get('verdicts',[]):
        print("  [%s] %s: %s"%(verdict,tag,msg));  nfail += (verdict=="FAIL")
    print("  → %s"%("%d CHECK(S) FAILED — do NOT claim this is fixed"%nfail if nfail else "all checks passed"))
    return nfail

def do_diff(a_path,b_path):
    from PIL import ImageChops
    a=load(a_path).convert("RGB"); b=load(b_path).convert("RGB")
    if a.size!=b.size: b=b.resize(a.size)
    diff=ImageChops.difference(a,b)
    bb=diff.getbbox()
    # quantify change
    px=diff.load(); w,h=diff.size; changed=0; tot=w*h
    for y in range(h):
        for x in range(w):
            r,g,bl=px[x,y]
            if r+g+bl>30: changed+=1
    pct=100.0*changed/tot
    # side by side + diff heat
    combo=Image.new("RGB",(a.width*3+20,a.height),(20,20,26))
    combo.paste(a,(0,0)); combo.paste(b,(a.width+10,0))
    heat=diff.point(lambda v: min(255,v*4)); combo.paste(heat,(a.width*2+20,0))
    out=os.path.splitext(b_path)[0]+"_diff.png"; combo.save(out)
    print("── diff: %s → %s ──"%(os.path.basename(a_path),os.path.basename(b_path)))
    print("  %.2f%% of pixels changed; change region bbox=%s"%(pct,bb))
    if pct < 0.5:
        print("  ⚠ NEARLY IDENTICAL — if you claimed a fix, the render barely changed. Verify you actually changed what you think.")
    print("  wrote %s (before | after | diff-heat)"%out)
    return out

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("images",nargs="+")
    ap.add_argument("--diff",action="store_true")
    ap.add_argument("--figure",action="store_true")
    a=ap.parse_args()
    if a.diff:
        if len(a.images)<2: print("--diff needs two images"); return 1
        do_diff(a.images[0],a.images[1]); return 0
    for p in a.images:
        im=load(p)
        R=measure_figure(im)
        n=print_report(os.path.basename(p),R)
        out=os.path.splitext(p)[0]+"_inspected.png"
        annotate(im,R,out); print("  annotated → %s\n"%out)
    return 0

if __name__=="__main__":
    sys.exit(main())
