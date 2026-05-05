"""
generate_menu_buttons.py
Auto-generates LVGL 8.3 RGB565+Alpha C-array assets for CrowPanel 2.1 menu buttons.

Canvas: 64x64 px, RGBA
Output: tools/preview/*.png  +  main/assets/*.c  +  main/assets/menu_button_assets.h

Run: pip install Pillow && python tools/generate_menu_buttons.py
"""

import math
import os
import struct
from PIL import Image, ImageDraw, ImageFilter, ImageEnhance

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
PREVIEW_DIR = os.path.join(SCRIPT_DIR, "preview")
ASSETS_DIR = os.path.join(PROJECT_ROOT, "main", "assets")

os.makedirs(PREVIEW_DIR, exist_ok=True)
os.makedirs(ASSETS_DIR, exist_ok=True)

SIZE = 64
HALF = SIZE // 2
CIRCLE_RADIUS = 30
SCALE = SIZE / 128.0


def s(v: float) -> int:
    """Scale values from the original 128px design space to current SIZE."""
    return max(1, int(round(v * SCALE)))

# ---------------------------------------------------------------------------
# Colour helpers
# ---------------------------------------------------------------------------

def lerp_color(c0, c1, t):
    """Linearly interpolate between two (r,g,b) tuples."""
    return tuple(int(c0[i] + (c1[i] - c0[i]) * t) for i in range(3))


def hex2rgb(h: str):
    h = h.lstrip("#")
    return tuple(int(h[i:i+2], 16) for i in (0, 2, 4))


def clamp(v, lo=0, hi=255):
    return max(lo, min(hi, int(v)))


# ---------------------------------------------------------------------------
# Build a radial-gradient filled circle (RGBA Image)
# ---------------------------------------------------------------------------

def make_circle_base(center_hex: str, edge_hex: str) -> Image.Image:
    """
    64x64 RGBA image.
    Inside circle radius 30: radial gradient center→edge with gamma 0.7.
    Outside: fully transparent.
    """
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    pixels = img.load()
    c0 = hex2rgb(center_hex)
    c1 = hex2rgb(edge_hex)

    for y in range(SIZE):
        for x in range(SIZE):
            dx = x - HALF
            dy = y - HALF
            dist = math.sqrt(dx * dx + dy * dy)
            if dist <= CIRCLE_RADIUS:
                t = pow(dist / CIRCLE_RADIUS, 0.7)
                r, g, b = lerp_color(c0, c1, t)
                pixels[x, y] = (clamp(r), clamp(g), clamp(b), 255)
            # else remains transparent
    return img


# ---------------------------------------------------------------------------
# Inner-rim highlight (drawn on selected state)
# ---------------------------------------------------------------------------

def draw_inner_rim(img: Image.Image, rim_radius: int = s(58), alpha: int = 180):
    """Draw a 2px semi-transparent white arc as a selection indicator."""
    overlay = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    box = [HALF - rim_radius, HALF - rim_radius,
           HALF + rim_radius, HALF + rim_radius]
    draw.arc(box, 0, 360, fill=(255, 255, 255, alpha), width=2)
    return Image.alpha_composite(img, overlay)


# ---------------------------------------------------------------------------
# Icon drawing functions
# ---------------------------------------------------------------------------

def draw_power_icon(draw: ImageDraw.Draw, color, cx=HALF, cy=HALF):
    """Classic power symbol: circle with gap at top, vertical line through gap."""
    arc_r = s(22)
    line_len = s(14)
    gap_deg = 60  # gap at top in degrees

    # vertical line from center upward through the gap
    draw.line(
        [(cx, cy - s(6)), (cx, cy - arc_r - line_len + s(4))],
        fill=color, width=max(1, s(4))
    )

    # arc: start after gap, go around, stop before gap
    half_gap = gap_deg / 2
    start_angle = 270 + half_gap   # gap centred at top (270°)
    end_angle   = 270 - half_gap + 360
    box = [cx - arc_r, cy - arc_r, cx + arc_r, cy + arc_r]
    draw.arc(box, start=start_angle, end=end_angle, fill=color, width=max(1, s(5)))


def draw_sun_icon(draw: ImageDraw.Draw, cx=HALF, cy=HALF):
    """Solid center + 8 wedge-shaped rays."""
    body_r = s(18)
    ray_inner_r = s(26)
    ray_outer_r = s(46)
    n_rays = 8
    body_color = (255, 240, 90, 255)     # bright warm yellow
    ray_color  = (255, 245, 140, 255)    # slightly lighter

    # rays (wedge = polygon)
    for i in range(n_rays):
        angle_center = math.radians(i * (360 / n_rays))
        half_width   = math.radians(7)   # half-angle of base width

        # inner arc edge points
        ax = cx + ray_inner_r * math.cos(angle_center - half_width)
        ay = cy + ray_inner_r * math.sin(angle_center - half_width)
        bx = cx + ray_inner_r * math.cos(angle_center + half_width)
        by = cy + ray_inner_r * math.sin(angle_center + half_width)

        # tip point
        tx = cx + ray_outer_r * math.cos(angle_center)
        ty = cy + ray_outer_r * math.sin(angle_center)

        draw.polygon([(ax, ay), (bx, by), (tx, ty)], fill=ray_color)

    # center disk
    draw.ellipse(
        [cx - body_r, cy - body_r, cx + body_r, cy + body_r],
        fill=body_color
    )


def draw_sparkle_icon(draw: ImageDraw.Draw, cx=HALF, cy=HALF):
    """Baked 'FX' text (replaces star glyph per UX request).

    Uses IBM Plex Sans Medium @ 60px from /tmp/lvfonts so rendering matches
    the UI body font. Colour is the same warm white the old star used.
    Text is manually centred via getbbox().
    """
    from PIL import ImageFont
    text_color = (255, 255, 255, 255)
    font_path = "/tmp/lvfonts/IBMPlexSans-Medium.ttf"
    try:
        font = ImageFont.truetype(font_path, s(58))
    except OSError:
        # Fallback for dev environments where /tmp/lvfonts is not present.
        font = ImageFont.load_default()
    text = "FX"
    # Use textbbox for pixel-accurate centring (PIL >=9.2 / Pillow 12).
    l, t, r, b = draw.textbbox((0, 0), text, font=font)
    w = r - l
    h = b - t
    x = cx - w // 2 - l
    y = cy - h // 2 - t
    draw.text((x, y), text, fill=text_color, font=font)


def draw_play_icon(draw: ImageDraw.Draw, color, cx=HALF, cy=HALF):
    """Filled right-pointing triangle, visually centered (offset 4px left)."""
    half_h = s(22)
    tip_x  = cx + s(18)
    left_x = cx - s(14)
    draw.polygon(
        [(left_x, cy - half_h), (left_x, cy + half_h), (tip_x, cy)],
        fill=color
    )


def draw_gear_icon(draw: ImageDraw.Draw, color, cx=HALF, cy=HALF):
    """8-tooth cogwheel: outer 30, inner 20, bore 10 (scaled)."""
    outer_r  = s(30)
    inner_r  = s(20)
    bore_r   = s(10)
    n_teeth  = 8
    tooth_w  = math.radians(12)   # half angular width of each tooth

    # build gear polygon
    pts = []
    for i in range(n_teeth):
        base_angle = math.radians(i * (360 / n_teeth))

        # leading inner point
        a0 = base_angle - tooth_w * 1.8
        pts.append((cx + inner_r * math.cos(a0), cy + inner_r * math.sin(a0)))

        # outer tooth corners
        a1 = base_angle - tooth_w
        a2 = base_angle + tooth_w
        pts.append((cx + outer_r * math.cos(a1), cy + outer_r * math.sin(a1)))
        pts.append((cx + outer_r * math.cos(a2), cy + outer_r * math.sin(a2)))

        # trailing inner point
        a3 = base_angle + tooth_w * 1.8
        pts.append((cx + inner_r * math.cos(a3), cy + inner_r * math.sin(a3)))

    draw.polygon(pts, fill=color)

    # bore hole (transparent)
    draw.ellipse(
        [cx - bore_r, cy - bore_r, cx + bore_r, cy + bore_r],
        fill=(0, 0, 0, 0)
    )


# ---------------------------------------------------------------------------
# Pixel-format conversion: RGBA → RGB565LE + Alpha8
# ---------------------------------------------------------------------------

def rgba_to_rgb565_alpha(img: Image.Image) -> bytes:
    """
    Returns bytes: [R5G6B5_lo, R5G6B5_hi, A8] per pixel, row-major.
    """
    img = img.convert("RGBA")
    w, h = img.size
    data = bytearray()
    px = img.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            lo = rgb565 & 0xFF
            hi = (rgb565 >> 8) & 0xFF
            data += bytes([lo, hi, a])
    return bytes(data)


# ---------------------------------------------------------------------------
# C-file writer
# ---------------------------------------------------------------------------

def write_c_file(name: str, pixel_data: bytes):
    path = os.path.join(ASSETS_DIR, f"{name}.c")
    total = len(pixel_data)
    lines = []

    # header comment
    lines.append(f"// Auto-generated by tools/generate_menu_buttons.py")
    lines.append(f"// Button: {name} — 128x128 LVGL RGB565 + Alpha")
    lines.append("")
    lines.append('#include "lvgl.h"')
    lines.append("")
    lines.append("#ifndef LV_ATTRIBUTE_MEM_ALIGN")
    lines.append("    #define LV_ATTRIBUTE_MEM_ALIGN")
    lines.append("#endif")
    lines.append("")
    lines.append(f"static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {name}_data[] = {{")
    lines.append(f"    // [R5G6B5_lo, R5G6B5_hi, A8] per pixel, row-major, {SIZE}*{SIZE}*3 = {total} bytes")

    # format pixel data: 12 triples per line (36 bytes → comfortable line length)
    TRIPLES_PER_LINE = 12
    for offset in range(0, total, TRIPLES_PER_LINE * 3):
        chunk = pixel_data[offset: offset + TRIPLES_PER_LINE * 3]
        hex_vals = ",".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_vals},")

    lines.append("};")
    lines.append("")
    lines.append(f"const lv_img_dsc_t {name} = {{")
    lines.append("    .header = {")
    lines.append("        .cf = LV_IMG_CF_TRUE_COLOR_ALPHA,")
    lines.append("        .always_zero = 0,")
    lines.append("        .reserved = 0,")
    lines.append(f"        .w = {SIZE},")
    lines.append(f"        .h = {SIZE},")
    lines.append("    },")
    lines.append(f"    .data_size = {SIZE} * {SIZE} * 3,")
    lines.append(f"    .data = {name}_data,")
    lines.append("};")
    lines.append("")

    with open(path, "w") as f:
        f.write("\n".join(lines))


# ---------------------------------------------------------------------------
# Save PNG preview
# ---------------------------------------------------------------------------

def save_preview(img: Image.Image, name: str):
    path = os.path.join(PREVIEW_DIR, f"{name}.png")
    img.save(path, "PNG")


# ---------------------------------------------------------------------------
# Make selected version from normal image
# ---------------------------------------------------------------------------

def make_selected(normal_img: Image.Image) -> Image.Image:
    """Brighten + add inner rim."""
    # work on a copy
    brightened = ImageEnhance.Brightness(normal_img).enhance(1.2)
    result = draw_inner_rim(brightened, rim_radius=s(58), alpha=180)
    return result


# ---------------------------------------------------------------------------
# Button factory
# ---------------------------------------------------------------------------

def make_button(name_normal: str, name_selected: str,
                center_hex: str, edge_hex: str,
                draw_icon_fn):
    """
    Builds normal + selected images for one button, saves PNG + .c file.
    draw_icon_fn receives (draw, img) and draws the icon in place.
    """

    # --- NORMAL ---
    base = make_circle_base(center_hex, edge_hex)
    draw = ImageDraw.Draw(base, "RGBA")
    draw_icon_fn(draw, base)
    save_preview(base, name_normal)
    pix = rgba_to_rgb565_alpha(base)
    write_c_file(name_normal, pix)
    print(f"Generated: {name_normal}.c  ({len(pix)} bytes)")

    # --- SELECTED ---
    sel = make_selected(base)
    save_preview(sel, name_selected)
    pix_sel = rgba_to_rgb565_alpha(sel)
    write_c_file(name_selected, pix_sel)
    print(f"Generated: {name_selected}.c  ({len(pix_sel)} bytes)")


# ---------------------------------------------------------------------------
# Per-button icon lambdas
# ---------------------------------------------------------------------------

def icon_power_off(draw, img):
    draw_power_icon(draw, hex2rgb("#C0CBDB") + (255,))

def icon_power_on(draw, img):
    draw_power_icon(draw, hex2rgb("#E8FFE8") + (255,))

def icon_sun(draw, img):
    draw_sun_icon(draw)

def icon_fx(draw, img):
    draw_sparkle_icon(draw)

def icon_play(draw, img):
    dark_color = hex2rgb("#06212B") + (255,)
    draw_play_icon(draw, dark_color)

def icon_settings(draw, img):
    gear_color = hex2rgb("#B8FFC8") + (255,)
    draw_gear_icon(draw, gear_color)


# ---------------------------------------------------------------------------
# Header file writer
# ---------------------------------------------------------------------------

def write_header():
    path = os.path.join(ASSETS_DIR, "menu_button_assets.h")
    names = [
        "btn_power_off_normal", "btn_power_off_selected",
        "btn_power_on_normal",  "btn_power_on_selected",
        "btn_sun_normal",       "btn_sun_selected",
        "btn_fx_normal",        "btn_fx_selected",
        "btn_play_normal",      "btn_play_selected",
        "btn_settings_normal",  "btn_settings_selected",
    ]
    lines = [
        "#pragma once",
        '#include "lvgl.h"',
        "",
    ]
    for n in names:
        lines.append(f"extern const lv_img_dsc_t {n};")
    lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))
    print(f"Generated: menu_button_assets.h")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == "__main__":

    # 1. Power OFF  — dark slate
    make_button(
        "btn_power_off_normal", "btn_power_off_selected",
        "#3F4A5C", "#2A3240",
        icon_power_off
    )

    # 2. Power ON  — vivid green
    make_button(
        "btn_power_on_normal", "btn_power_on_selected",
        "#1BB86A", "#0D7040",
        icon_power_on
    )

    # 3. Sun  — deep warm orange
    make_button(
        "btn_sun_normal", "btn_sun_selected",
        "#FF8A2A", "#B04010",
        icon_sun
    )

    # 4. FX  — hot magenta
    make_button(
        "btn_fx_normal", "btn_fx_selected",
        "#D83B8C", "#8B1055",
        icon_fx
    )

    # 5. Play  — cyan
    make_button(
        "btn_play_normal", "btn_play_selected",
        "#18B4D1", "#0A607A",
        icon_play
    )

    # 6. Settings  — forest green
    make_button(
        "btn_settings_normal", "btn_settings_selected",
        "#1F7A4C", "#0E4028",
        icon_settings
    )

    write_header()

    print("\nDone. All 12 button assets generated.")
    print(f"  PNGs  → {PREVIEW_DIR}")
    print(f"  C files → {ASSETS_DIR}")
