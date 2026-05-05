#!/usr/bin/env python3
"""Render baked SUSE-style backgrounds and convert them to lv_img_dsc_t C
arrays for LVGL 8.3 (LV_IMG_CF_TRUE_COLOR, RGB565, no alpha).

Design direction (per designer skill):
- 480x480 static asset per screen, no runtime gradient / shadow / radius.
- Dark outer rim -> brighter off-center highlight. "Light from inside".
- One low-frequency accent band / ellipse for recognisability.
- Per-screen palette: base, mid-ish highlight, accent, and (optionally) an
  overlay tint for the geometric band.

Output:
- main/assets/bg/bg_<screen>.png  (for debugging / design review)
- main/assets/bg/bg_<screen>.c    (lv_img_dsc_t, RGB565 little-endian)
- main/assets/bg/bg_assets.h      (LV_IMG_DECLARE for every generated bg)

Run from repo root:
    .venv/bin/python scripts/render_backgrounds.py
"""

from __future__ import annotations

import math
import os
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw, ImageFilter


REPO_ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = REPO_ROOT / "main" / "assets" / "bg"
OUT_DIR.mkdir(parents=True, exist_ok=True)

SIZE = 240


# ---------------------------------------------------------------------------
# Palette definitions
# ---------------------------------------------------------------------------
@dataclass
class Palette:
    """One screen's color recipe.

    All colors are sRGB (R, G, B) 0..255. ``highlight_xy`` is a relative
    location for the light centre (0..1, 0..1 in image space).

    Visual language (SUSE-wallpaper family):
      - radial gradient base (rim -> mid -> highlight)
      - "flow-field" streamlines from a sin/cos vector field — those give
        the recognisable mathematical ribbon look
      - bokeh accent dots layered in the accent colour
      - subtle outer vignette for the round display
    """

    name: str
    rim: tuple[int, int, int]
    mid: tuple[int, int, int]
    highlight: tuple[int, int, int]
    band: tuple[int, int, int]         # accent / curve / bokeh colour
    highlight_xy: tuple[float, float] = (0.5, 0.42)
    rim_radius: float = 0.96     # fraction of half-diagonal to reach "rim"

    # Flow-field parameters. Each palette gets a unique motif by varying
    # frequency and phase of the sin/cos vector field.
    flow_freq_a: float = 0.0040   # primary x frequency
    flow_freq_b: float = 0.0055   # primary y frequency
    flow_phase: float = 0.0       # radians
    flow_lines: int = 70          # number of streamlines
    flow_steps: int = 220         # integration length per line
    flow_opacity: int = 70        # 0..255
    flow_width: int = 2           # px

    # Bokeh accent dots.
    bokeh_count: int = 38
    bokeh_r_min: int = 6
    bokeh_r_max: int = 36
    bokeh_opacity: int = 70       # 0..255 peak
    seed: int = 1


PALETTES: list[Palette] = [
    # Main menu: deep green -> bright teal highlight (echoes WLED logo family)
    Palette(
        name="main",
        rim=(6, 26, 20),
        mid=(14, 60, 44),
        highlight=(40, 140, 108),
        band=(150, 230, 190),
        highlight_xy=(0.50, 0.42),
        flow_freq_a=0.0042, flow_freq_b=0.0058, flow_phase=0.0,
        flow_lines=78, flow_opacity=62,
        bokeh_count=42, seed=11,
    ),
    # Alarm: night-sky -> sky blue.
    Palette(
        name="alarm",
        rim=(6, 16, 34),
        mid=(20, 56, 110),
        highlight=(80, 160, 230),
        band=(170, 220, 255),
        highlight_xy=(0.50, 0.55),
        flow_freq_a=0.0036, flow_freq_b=0.0050, flow_phase=0.7,
        flow_lines=82, flow_opacity=58,
        bokeh_count=48, seed=23,
    ),
    # Brightness: sun-warm amber.
    Palette(
        name="brightness",
        rim=(24, 12, 4),
        mid=(90, 50, 10),
        highlight=(240, 180, 60),
        band=(255, 230, 140),
        highlight_xy=(0.50, 0.42),
        flow_freq_a=0.0048, flow_freq_b=0.0060, flow_phase=1.4,
        flow_lines=74, flow_opacity=74,
        bokeh_count=44, seed=37,
    ),
    # Effects: deep violet -> magenta (FX / music association).
    Palette(
        name="effects",
        rim=(14, 4, 30),
        mid=(60, 20, 100),
        highlight=(160, 80, 200),
        band=(230, 150, 245),
        highlight_xy=(0.50, 0.45),
        flow_freq_a=0.0054, flow_freq_b=0.0046, flow_phase=2.1,
        flow_lines=86, flow_opacity=70,
        bokeh_count=54, seed=53,
    ),
    # Presets: teal / aqua (playback family).
    Palette(
        name="presets",
        rim=(2, 22, 28),
        mid=(8, 70, 80),
        highlight=(40, 160, 170),
        band=(150, 235, 230),
        highlight_xy=(0.50, 0.42),
        flow_freq_a=0.0040, flow_freq_b=0.0064, flow_phase=2.8,
        flow_lines=72, flow_opacity=66,
        bokeh_count=40, seed=67,
    ),
    # Settings: slate with cool highlight.
    Palette(
        name="settings",
        rim=(12, 14, 18),
        mid=(40, 48, 58),
        highlight=(130, 150, 170),
        band=(200, 215, 235),
        highlight_xy=(0.50, 0.42),
        flow_freq_a=0.0032, flow_freq_b=0.0042, flow_phase=3.5,
        flow_lines=64, flow_opacity=54,
        bokeh_count=34, seed=89,
    ),
    # Mic: dark violet, slight red accent.
    Palette(
        name="mic",
        rim=(16, 6, 20),
        mid=(60, 20, 60),
        highlight=(160, 80, 140),
        band=(235, 140, 190),
        highlight_xy=(0.50, 0.45),
        flow_freq_a=0.0060, flow_freq_b=0.0052, flow_phase=4.2,
        flow_lines=80, flow_opacity=68,
        bokeh_count=46, seed=103,
    ),
    # Info: anthracite, cool blueish highlight.
    Palette(
        name="info",
        rim=(8, 10, 14),
        mid=(30, 36, 46),
        highlight=(90, 120, 150),
        band=(160, 190, 220),
        highlight_xy=(0.50, 0.42),
        flow_freq_a=0.0030, flow_freq_b=0.0040, flow_phase=5.0,
        flow_lines=60, flow_opacity=52,
        bokeh_count=32, seed=131,
    ),
]


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------
def _lerp(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    t = max(0.0, min(1.0, t))
    return (
        int(a[0] + (b[0] - a[0]) * t),
        int(a[1] + (b[1] - a[1]) * t),
        int(a[2] + (b[2] - a[2]) * t),
    )


def render_background(pal: Palette) -> Image.Image:
    """Compose a SUSE-style baked background for a single screen.

    Layer order (bottom -> top):
      1. Radial gradient (rim -> mid -> highlight, light from within).
      2. Flow-field streamlines following a sin/cos vector field,
         in ``pal.band`` colour — the signature mathematical ribbon look.
      3. Bokeh accent dots (soft blurred circles) in ``pal.band``.
      4. Soft circular vignette to frame the round panel.
    """

    import random

    cx = int(pal.highlight_xy[0] * SIZE)
    cy = int(pal.highlight_xy[1] * SIZE)
    max_r = math.hypot(max(cx, SIZE - cx), max(cy, SIZE - cy))
    rim_r = max_r * pal.rim_radius

    # --- Layer 1: radial gradient --------------------------------------
    img = Image.new("RGB", (SIZE, SIZE), pal.rim)
    pixels = img.load()
    mid_r = rim_r * 0.55
    for y in range(SIZE):
        dy = y - cy
        for x in range(SIZE):
            dx = x - cx
            r = math.hypot(dx, dy)
            if r <= mid_r:
                t = r / mid_r
                pixels[x, y] = _lerp(pal.highlight, pal.mid, t)
            elif r <= rim_r:
                t = (r - mid_r) / (rim_r - mid_r)
                pixels[x, y] = _lerp(pal.mid, pal.rim, t)
            # else: stays rim color (corners)

    rng = random.Random(pal.seed)

    # --- Layer 2: flow-field streamlines -------------------------------
    # Vector field: angle(x, y) = sin(a*x + phi) + cos(b*y + phi)
    # Integrate forward and backward from random seeds to get ribbons
    # that bend gently across the frame (SUSE wallpaper signature).
    flow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    fd = ImageDraw.Draw(flow)

    def angle_at(x: float, y: float) -> float:
        return (
            math.sin(pal.flow_freq_a * x + pal.flow_phase) * math.pi
            + math.cos(pal.flow_freq_b * y + pal.flow_phase * 0.7) * math.pi
        ) * 0.5

    step_len = 3.2
    for _ in range(pal.flow_lines):
        sx = rng.uniform(-40, SIZE + 40)
        sy = rng.uniform(-40, SIZE + 40)
        # Random opacity jitter for depth.
        opa = max(20, min(255, int(pal.flow_opacity * rng.uniform(0.55, 1.15))))
        col = (*pal.band, opa)
        for direction in (1, -1):
            x, y = sx, sy
            prev = (x, y)
            for _step in range(pal.flow_steps // 2):
                a = angle_at(x, y)
                x += math.cos(a) * step_len * direction
                y += math.sin(a) * step_len * direction
                if -60 <= x <= SIZE + 60 and -60 <= y <= SIZE + 60:
                    fd.line([prev, (x, y)], fill=col, width=pal.flow_width)
                    prev = (x, y)
                else:
                    break
    # Soften the flow layer so it reads as an airbrushed pattern, not
    # vector strokes.
    flow = flow.filter(ImageFilter.GaussianBlur(radius=1.6))
    img = Image.alpha_composite(img.convert("RGBA"), flow)

    # --- Layer 3: bokeh accent dots ------------------------------------
    bokeh = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bokeh)
    for _ in range(pal.bokeh_count):
        # Prefer dots roughly around the highlight (softer glow look).
        bx = int(rng.gauss(cx, SIZE * 0.28))
        by = int(rng.gauss(cy, SIZE * 0.28))
        r = rng.randint(pal.bokeh_r_min, pal.bokeh_r_max)
        a = int(pal.bokeh_opacity * rng.uniform(0.35, 1.0))
        bd.ellipse((bx - r, by - r, bx + r, by + r), fill=(*pal.band, a))
    bokeh = bokeh.filter(ImageFilter.GaussianBlur(radius=6.5))
    img = Image.alpha_composite(img, bokeh)

    # --- Layer 4: round-panel vignette ---------------------------------
    vignette = Image.new("L", (SIZE, SIZE), 0)
    vd = ImageDraw.Draw(vignette)
    vd.ellipse((-40, -40, SIZE + 40, SIZE + 40), fill=255)
    vd.ellipse((20, 20, SIZE - 20, SIZE - 20), fill=0)
    vignette = vignette.filter(ImageFilter.GaussianBlur(radius=40))
    dark = Image.new("RGB", (SIZE, SIZE), (0, 0, 0)).convert("RGBA")
    img_rgb = img.convert("RGB")
    img_rgb = Image.composite(dark.convert("RGB"), img_rgb, vignette)

    return img_rgb


# ---------------------------------------------------------------------------
# PNG -> lv_img_dsc_t (RGB565, little-endian, 16bpp, no alpha)
# ---------------------------------------------------------------------------
def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def image_to_rgb565_bytes(img: Image.Image) -> bytes:
    assert img.mode == "RGB"
    out = bytearray(SIZE * SIZE * 2)
    px = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b = px[x, y]
            v = rgb565(r, g, b)
            # LVGL 8.3 RGB565 is stored little-endian.
            out[(y * SIZE + x) * 2 + 0] = v & 0xFF
            out[(y * SIZE + x) * 2 + 1] = (v >> 8) & 0xFF
    return bytes(out)


C_HEADER = """// Auto-generated by scripts/render_backgrounds.py -- do not edit by hand.
#include "lvgl.h"

#if LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP != 0
#error "bg_*.c assumes LV_COLOR_DEPTH=16, LV_COLOR_16_SWAP=0 (little-endian RGB565)"
#endif

"""


def emit_c(pal_name: str, data: bytes) -> str:
    sym = f"bg_{pal_name}"
    lines: list[str] = [C_HEADER]
    lines.append(f"static const LV_ATTRIBUTE_LARGE_CONST uint8_t {sym}_map[] = {{\n")
    row = []
    for i, b in enumerate(data):
        row.append(f"0x{b:02x}")
        if (i + 1) % 16 == 0:
            lines.append("    " + ",".join(row) + ",\n")
            row = []
    if row:
        lines.append("    " + ",".join(row) + ",\n")
    lines.append("};\n\n")
    lines.append(
        f"const lv_img_dsc_t {sym} = {{\n"
        "    .header.cf = LV_IMG_CF_TRUE_COLOR,\n"
        "    .header.always_zero = 0,\n"
        "    .header.reserved = 0,\n"
        f"    .header.w = {SIZE},\n"
        f"    .header.h = {SIZE},\n"
        f"    .data_size = {len(data)},\n"
        f"    .data = {sym}_map,\n"
        "};\n"
    )
    return "".join(lines)


def emit_header(names: Iterable[str]) -> str:
    lines = [
        "// Auto-generated by scripts/render_backgrounds.py -- do not edit by hand.\n",
        "#pragma once\n\n",
        "#ifdef __cplusplus\n",
        'extern "C" {\n',
        "#endif\n\n",
        "#include \"lvgl.h\"\n\n",
    ]
    for n in names:
        lines.append(f"LV_IMG_DECLARE(bg_{n});\n")
    lines.append("\n#ifdef __cplusplus\n}\n#endif\n")
    return "".join(lines)


def main() -> None:
    for pal in PALETTES:
        img = render_background(pal)
        png_path = OUT_DIR / f"bg_{pal.name}.png"
        img.save(png_path, "PNG")
        data = image_to_rgb565_bytes(img)
        c_path = OUT_DIR / f"bg_{pal.name}.c"
        c_path.write_text(emit_c(pal.name, data))
        print(f"rendered {pal.name:10s} -> {c_path.relative_to(REPO_ROOT)}  ({len(data)/1024:.1f} KB)")

    (OUT_DIR / "bg_assets.h").write_text(emit_header(p.name for p in PALETTES))
    print(f"header   -> {(OUT_DIR / 'bg_assets.h').relative_to(REPO_ROOT)}")


if __name__ == "__main__":
    main()
