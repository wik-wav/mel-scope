"""Generate src/Colormaps.h: 256-entry RGB tables for the plugin.

Inferno/Magma/Plasma/Viridis/Turbo come from matplotlib (exact).
"Ember" is fitted from the reference screenshot's own colour ramp: the
screenshot is dominated by one background colour and a magenta->orange ramp,
so we sort its lit pixels by luminance and take the median RGB per bin.
"""
import io
import os
import re
import sys
import numpy as np
import matplotlib.cm as cm
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "src", "Colormaps.h")

# Ember is derived from a reference screenshot; pass its path as argv[1]. Without it
# every other map is still generated exactly (they come from matplotlib).
REF = sys.argv[1] if len(sys.argv) > 1 else None


def ember_stops():
    if not REF:
        # Without the reference screenshot, reuse the table already in the header so the
        # script still regenerates everything else reproducibly.
        try:
            src = io.open(OUT, encoding="utf-8").read()
            body = re.search(r"kMapEmber\[256\]\[3\] = \{(.*?)\};", src, re.S).group(1)
            rows = re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", body)
            stops = [tuple(int(v) for v in row) for row in rows]
            print(f"no reference image: reusing the {len(stops)} Ember stops from {os.path.basename(OUT)}")
            return stops
        except Exception as exc:
            raise SystemExit(f"Ember needs the reference image (pass its path as an argument): {exc}")
    a = np.asarray(Image.open(REF).convert("RGB")).astype(np.float32)
    flat = a.reshape(-1, 3).astype(np.uint8)
    cols, counts = np.unique(flat, axis=0, return_counts=True)
    bg = cols[np.argmax(counts)].astype(np.float32)
    d = np.sqrt(((a - bg) ** 2).sum(-1))
    px = a[d > 25]
    lum = 0.2126 * px[:, 0] + 0.7152 * px[:, 1] + 0.0722 * px[:, 2]
    lo, hi = np.percentile(lum, 0.2), np.percentile(lum, 99.99)
    N = 44
    stops = [tuple(int(v) for v in bg)]
    for i in range(N):
        a0 = lo + (hi - lo) * i / N
        a1 = lo + (hi - lo) * (i + 1) / N
        sel = px[(lum >= a0) & (lum < a1)]
        if len(sel) == 0:
            continue
        med = np.median(sel, axis=0)
        stops.append((int(med[0]), int(med[1]), int(med[2])))
    # monotone-ish cleanup: enforce non-decreasing luminance
    out = [stops[0]]
    for c in stops[1:]:
        if c != out[-1]:
            out.append(c)
    # extend the hot end: the screenshot's few brightest pixels are sparse, so the
    # binned medians stop a little short of the true top of the ramp.
    for tail in [(241, 174, 82), (244, 193, 96), (246, 211, 109), (248, 228, 122),
                 (250, 243, 134), (252, 250, 168), (255, 253, 205)]:
        if 0.2126 * out[-1][0] + 0.7152 * out[-1][1] + 0.0722 * out[-1][2] < \
           0.2126 * tail[0] + 0.7152 * tail[1] + 0.0722 * tail[2]:
            out.append(tail)
    return out


def resample(stops, n=256):
    """stops: list of (r,g,b) control points -> n-entry table via linear interp."""
    stops = np.asarray(stops, dtype=np.float64)
    xs = np.linspace(0.0, 1.0, len(stops))
    t = np.linspace(0.0, 1.0, n)
    tab = np.stack([np.interp(t, xs, stops[:, i]) for i in range(3)], axis=1)
    return np.clip(np.round(tab), 0, 255).astype(np.uint8)


def mpl(name, n=256):
    import matplotlib
    cmap = matplotlib.colormaps[name]
    t = np.linspace(0.0, 1.0, n)
    rgba = np.array([cmap(v) for v in t])
    return np.clip(np.round(rgba[:, :3] * 255.0), 0, 255).astype(np.uint8)


def main():
    maps = [
        ("Ember", resample(ember_stops())),
        ("Inferno", mpl("inferno")),
        ("Magma", mpl("magma")),
        ("Plasma", mpl("plasma")),
        ("Viridis", mpl("viridis")),
        ("Turbo", mpl("turbo")),
        ("Cividis", mpl("cividis")),
        ("Ice", resample([
            (10, 12, 20), (12, 30, 60), (10, 70, 110), (20, 130, 160),
            (60, 190, 190), (150, 230, 220), (230, 250, 250), (255, 255, 255)])),
        # light maps: quiet is white/paper, loud is ink -- matches the light UI
        ("Paper", resample([
            (255, 255, 255), (255, 255, 255), (243, 242, 239), (228, 226, 222),
            (185, 180, 173), (109, 106, 102), (43, 42, 51), (10, 11, 22)])),
        ("Sand", resample([
            (255, 255, 255), (255, 253, 250), (238, 226, 208), (206, 168, 120),
            (150, 100, 66), (66, 46, 36), (10, 11, 22)])),
        # ink-on-paper maps: cream ground, low-chroma pastel mid-tones, and only
        # the loudest content reaching dark ink (see _analysis/*ink*.png refs)
        ("Bloom", resample([
            (255, 255, 255), (252, 248, 238), (241, 232, 212), (235, 216, 194),
            (232, 203, 197), (221, 192, 213), (193, 183, 214), (148, 159, 205),
            (99, 117, 172), (58, 76, 134), (26, 40, 84), (10, 11, 22)])),
        ("Ink", resample([
            (255, 255, 255), (250, 251, 251), (238, 242, 244), (223, 232, 237),
            (201, 217, 227), (170, 199, 218), (129, 177, 204), (90, 147, 184),
            (54, 109, 157), (28, 70, 120), (15, 38, 78), (10, 11, 22)])),
    ]
    # Complementary ink for the traced peak lines: they must read as an overlay,
    # not as "the hot end of the gradient". For a chromatic map take the hue at
    # 55 % of the ramp, rotate it 180 degrees and keep it deep (light canvas) or
    # bright (dark canvas). Achromatic maps get a fixed warm annotation ink.
    import colorsys

    # Hand-picked peak inks for the light maps: a hue complement alone is not
    # enough, because these ramps run warm canvas -> cool body, so the complement
    # of the body (yellow) has poor contrast on the cream ground. These are the
    # vivid accents from the reference images (raspberry / teal / burnt orange),
    # deep enough to read as ink on paper.
    LINE_INK = {
        "Paper": (163, 18, 63),
        # Sand and Ink are the two light ramps whose complement read as a foreign
        # accent: a cold teal on warm tan, and a muddy brown-orange on pale blue. These
        # keep an ink weight while sitting further from every stop of their ramp.
        "Sand": (104, 48, 122),   # aubergine: warm-adjacent, dE 32 -> 50
        "Ink": (214, 84, 20),     # clean vermilion orange, dE 61 -> 76
        "Bloom": (166, 26, 92),

        # Turbo and Cividis contain every hue, so the automatic complement lands
        # inside the ramp (worst-case dE 23 and 43 against the ramp, its dimmed wash
        # and the canvas). These are the colours furthest from all 256 stops.
        "Turbo": (246, 16, 160),
        "Cividis": (226, 8, 190),
    }

    def line_colour(tab, light_bg, name=""):
        if name in LINE_INK:
            return LINE_INK[name]
        mid = tab[int(0.55 * 255)]
        r, g, b = [v / 255.0 for v in mid]
        h, l, s = colorsys.rgb_to_hls(r, g, b)
        if s < 0.06:
            return (166, 62, 44) if light_bg else (246, 196, 132)
        h2 = (h + 0.5) % 1.0
        l2 = 0.30 if light_bg else 0.66
        s2 = 0.85
        r2, g2, b2 = colorsys.hls_to_rgb(h2, l2, s2)
        return (int(round(r2 * 255)), int(round(g2 * 255)), int(round(b2 * 255)))


    # ------------------------------------------------------------------ note tint
    # MIDI notes are composited *under* the spectrum, at a low alpha that falls off
    # where the image is bright. Picking a colour by distance from the ramp alone is
    # not enough: at that alpha a tint can vanish into the wash it lands on, or read
    # as one of the peak traces. So the search runs on the *composited* colour, over
    # the backgrounds a note realistically sits on, and demands separation from both
    # the background and the peak ink.
    def _lab(rgb):
        def f(u):
            u /= 255.0
            return u / 12.92 if u <= 0.04045 else ((u + 0.055) / 1.055) ** 2.4
        r, g, b = (f(c) for c in rgb)
        x = (r * 0.4124 + g * 0.3576 + b * 0.1805) / 0.95047
        y = r * 0.2126 + g * 0.7152 + b * 0.0722
        z = (r * 0.0193 + g * 0.1192 + b * 0.9505) / 1.08883
        g_ = lambda t: t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
        fx, fy, fz = g_(x), g_(y), g_(z)
        return (116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz))

    def _de(a, b):
        la, lb = _lab(a), _lab(b)
        return sum((la[i] - lb[i]) ** 2 for i in range(3)) ** 0.5

    def _mix(bg, fg, a):
        return tuple(int(round(bg[i] + (fg[i] - bg[i]) * a)) for i in range(3))

    def note_tint(tab, ink, light_bg, name=""):
        # The renderer bands a note at alpha 0.42 * level (0.55 by default), so the
        # weakest note is drawn at about 0.23 and fades further where the image is
        # bright. Judge the choice at that alpha, over the backgrounds a note actually
        # lands on: the canvas and the quiet half of the ramp.
        A = 0.42 * 0.55
        bgs = [tab[0], tab[int(0.10 * 255)], tab[int(0.22 * 255)], tab[int(0.36 * 255)]]
        if light_bg:
            bgs.append((255, 255, 255))
        bg_lab = [_lab(c) for c in bgs]

        def chroma(c):
            l = _lab(c)
            return (l[1] ** 2 + l[2] ** 2) ** 0.5

        best, best_score = None, -1.0
        # coarse sweep, then refine around the winner: cheap and stable
        steps = [(8, 8, 8), (3, 3, 3), (1, 1, 1)]
        centre = None
        for step in steps:
            if centre is None:
                lo, hi = (0, 0, 0), (255, 255, 255)
            else:
                lo = tuple(max(0, centre[i] - 3 * step[i]) for i in range(3))
                hi = tuple(min(255, centre[i] + 3 * step[i]) for i in range(3))
            for r in range(lo[0], hi[0] + 1, step[0]):
                for g in range(lo[1], hi[1] + 1, step[1]):
                    for b in range(lo[2], hi[2] + 1, step[2]):
                        c = (r, g, b)
                        L = _lab(c)[0]
                        C = chroma(c)
                        # calm, not a highlighter: a modest tint with a mid value that
                        # sits behind the image instead of shouting over it
                        if C > 20.0:
                            continue
                        if light_bg and not (28.0 <= L <= 58.0):
                            continue
                        if (not light_bg) and not (66.0 <= L <= 92.0):
                            continue
                        vis = 1e9
                        for bg, lb in zip(bgs, bg_lab):
                            comp = _lab(_mix(bg, c, A))
                            d = sum((comp[i] - lb[i]) ** 2 for i in range(3)) ** 0.5
                            if d < vis:
                                vis = d
                        apart = _de(c, ink)          # must not read as a peak trace
                        # a mild pull towards neutral: colour should help, not decorate
                        calm = 1.0 - 0.10 * (C / 20.0)
                        score = min(vis, 0.55 * apart) * calm
                        if score > best_score:
                            best_score, best = score, c
            centre = best
        return best

    NOTE_TINT = {}

    lines = ["// Generated by _analysis/make_colormaps.py -- do not edit by hand.",
             "#pragma once",
             "#include <cstdint>",
             "",
             "namespace mel {",
             "",
             "enum class CMap : int { Ember = 0, Inferno, Magma, Plasma, Viridis, Turbo, Cividis, Ice, Count };",
             "",
             f"static const int kNumMaps = {len(maps)};",
             'static const char* const kMapNames[kNumMaps] = { ' + ", ".join(f'"{n}"' for n, _ in maps) + " };",
             ""]
    for name, tab in maps:
        lines.append(f"static const unsigned char kMap{name}[256][3] = {{")
        for i in range(256):
            r, g, b = tab[i]
            lines.append(f"  {{ {r:3d}, {g:3d}, {b:3d} }},")
        lines.append("};")
        lines.append("")
    # which maps are already "light canvas" by construction (quiet end is pale)
    light_flags = ", ".join("1" if n in ("Paper", "Sand", "Bloom", "Ink") else "0" for n, _ in maps)
    lines.append(f"static const unsigned char kMapIsLight[kNumMaps] = {{ {light_flags} }};")
    lines.append("")
    theme_of = {"Ice": 2}
    for n, _t in maps:
        theme_of.setdefault(n, 1 if n in ("Ember", "Inferno", "Magma", "Plasma", "Viridis", "Turbo", "Cividis") else 0)
    tags = ", ".join(str(theme_of[n]) for n, _t in maps)
    lines.append(f"// 0 = light theme only, 1 = dark theme only, 2 = both")
    lines.append(f"static const unsigned char kMapTheme[kNumMaps] = {{ {tags} }};")
    lines.append("")
    line_rows = ", ".join("{ %3d, %3d, %3d }" % line_colour(t, n not in ("Ember", "Inferno", "Magma", "Plasma", "Viridis", "Turbo", "Cividis", "Ice"), n) for n, t in maps)
    lines.append(f"static const unsigned char kMapLine[kNumMaps][3] = {{ {line_rows} }};")
    lines.append("")
    note_rows = ", ".join("{ %3d, %3d, %3d }" % note_tint(t, line_colour(t, n not in ("Ember", "Inferno", "Magma", "Plasma", "Viridis", "Turbo", "Cividis", "Ice"), n), n not in ("Ember", "Inferno", "Magma", "Plasma", "Viridis", "Turbo", "Cividis", "Ice"), n) for n, t in maps)
    lines.append(f"static const unsigned char kMapNote[kNumMaps][3] = {{ {note_rows} }};")
    lines.append("")
    lines.append("static const unsigned char (*const kMaps[kNumMaps])[3] = {")
    lines.append("    " + ", ".join(f"kMap{n}" for n, _ in maps) + " };")
    lines.append("")
    lines.append("} // namespace mel")
    lines.append("")
    with open(OUT, "w", newline="\n") as f:
        f.write("\n".join(lines))
    print("wrote", OUT, os.path.getsize(OUT), "bytes")
    print("ember stops:", ember_stops())

    # preview strip
    W, H = 256 * 3, 90
    im = Image.new("RGB", (W, H * len(maps)))
    dr = im.load()
    for m, (name, tab) in enumerate(maps):
        for x in range(W):
            c = tuple(int(v) for v in tab[min(255, x * 256 // W)])
            for y in range(H):
                dr[x, m * H + y] = c
    im.save(os.path.join(HERE, "colormap_preview.png"))
    print("wrote colormap_preview.png", [n for n, _ in maps])


main()
