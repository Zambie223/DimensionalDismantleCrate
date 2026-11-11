"""Draws the Dimensional Dismantle Crate icon: a slatted isometric crate sending items up into a small purple depot cube.

Writes Resources/Icon128.png (SML's Mods menu, fixed path and size) and Branding/Icon512.png (for the SMR logo, not shipped).
Pure drawing with Pillow, no external art. Run: py -3 Tools/make_icon.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter

SS = 4  # supersampling factor
BASE = 512
N = BASE * SS

# Crate: warm grey-brown slats with dark straps.
CRATE_TOP = (196, 160, 112)
CRATE_LEFT = (160, 122, 78)
CRATE_RIGHT = (122, 90, 56)
SLAT_LINE = (88, 62, 36)
EDGE = (40, 28, 16)
# Depot cube: purple, after the Dimensional Depot's in-game colour scheme.
CUBE_TOP = (190, 146, 255)
CUBE_LEFT = (142, 88, 232)
CUBE_RIGHT = (98, 54, 180)
CUBE_EDGE = (38, 18, 70)
WHITE = (248, 248, 248)
BG_TOP = (46, 52, 62)
BG_BOTTOM = (22, 25, 30)
BORDER = (70, 78, 92)


def s(v):
    return int(round(v * SS))


def rounded_mask(size, radius):
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size - 1, size - 1), radius=radius, fill=255)
    return mask


def background():
    grad = Image.new("RGB", (N, N))
    d = ImageDraw.Draw(grad)
    for y in range(N):
        t = y / (N - 1)
        d.line([(0, y), (N, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOTTOM)))
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    img.paste(grad, (0, 0), rounded_mask(N, s(96)))
    ImageDraw.Draw(img).rounded_rectangle((s(6), s(6), N - s(6), N - s(6)), radius=s(90), outline=BORDER, width=s(6))
    return img


def iso_points(cx, cy, r):
    dx = r * math.cos(math.radians(30))
    return {
        "top": (cx, cy - r),
        "ul": (cx - dx, cy - r / 2),
        "ur": (cx + dx, cy - r / 2),
        "c": (cx, cy),
        "ll": (cx - dx, cy + r / 2),
        "lr": (cx + dx, cy + r / 2),
        "bottom": (cx, cy + r),
    }


def lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def sc(pts):
    return [(s(x), s(y)) for x, y in pts]


def iso_box(draw, cx, cy, r, top, left, right, edge, edge_w, slats=0, slat_color=None):
    """Isometric box; (cx, cy) is the centre of the hexagon outline, r its circumradius. Optional slats on the sides."""
    p = iso_points(cx, cy, r)
    draw.polygon(sc([p["top"], p["ur"], p["c"], p["ul"]]), fill=top)
    draw.polygon(sc([p["ul"], p["c"], p["bottom"], p["ll"]]), fill=left)
    draw.polygon(sc([p["c"], p["ur"], p["lr"], p["bottom"]]), fill=right)
    if slats:
        for i in range(1, slats):
            t = i / slats
            draw.line(sc([lerp(p["ul"], p["ll"], t), lerp(p["c"], p["bottom"], t)]), fill=slat_color, width=s(4))
            draw.line(sc([lerp(p["c"], p["bottom"], t), lerp(p["ur"], p["lr"], t)]), fill=slat_color, width=s(4))
        # Corner posts give it the look of a crate rather than a plain box.
        post = 0.14
        for a, b, c2, d2 in ((p["ul"], p["ll"], p["c"], p["bottom"]), (p["c"], p["bottom"], p["ur"], p["lr"])):
            draw.polygon(sc([a, lerp(a, c2, post), lerp(b, d2, post), b]), fill=slat_color)
            draw.polygon(sc([lerp(a, c2, 1 - post), c2, d2, lerp(b, d2, 1 - post)]), fill=slat_color)
    w = s(edge_w)
    draw.line(sc([p["top"], p["ur"], p["lr"], p["bottom"], p["ll"], p["ul"], p["top"]]), fill=edge, width=w, joint="curve")
    draw.line(sc([p["ul"], p["c"], p["ur"]]), fill=edge, width=w, joint="curve")
    draw.line(sc([p["c"], p["bottom"]]), fill=edge, width=w)


def arrow(layer, start, tip, shaft_w, head_w, head_h, color):
    """Straight arrow from start to tip (512-space coordinates)."""
    d = ImageDraw.Draw(layer)
    dx, dy = tip[0] - start[0], tip[1] - start[1]
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length  # along the arrow
    nx, ny = -uy, ux  # across the arrow
    neck = (tip[0] - ux * head_h, tip[1] - uy * head_h)
    hw, hh = shaft_w / 2, head_w / 2
    d.polygon(sc([
        (start[0] + nx * hw, start[1] + ny * hw),
        (neck[0] + nx * hw, neck[1] + ny * hw),
        (neck[0] + nx * hh, neck[1] + ny * hh),
        tip,
        (neck[0] - nx * hh, neck[1] - ny * hh),
        (neck[0] - nx * hw, neck[1] - ny * hw),
        (start[0] - nx * hw, start[1] - ny * hw),
    ]), fill=color)


def with_shadow(img, layer, offset, blur, alpha):
    shadow = layer.split()[3].filter(ImageFilter.GaussianBlur(s(blur)))
    img.paste((0, 0, 0, alpha), (s(offset[0]), s(offset[1])), shadow)
    img.alpha_composite(layer)


def main():
    img = background()

    crate_layer = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    iso_box(ImageDraw.Draw(crate_layer), 196, 350, 132, CRATE_TOP, CRATE_LEFT, CRATE_RIGHT, EDGE, 7, slats=4, slat_color=SLAT_LINE)
    with_shadow(img, crate_layer, (6, 14), 10, 110)

    cube_layer = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    iso_box(ImageDraw.Draw(cube_layer), 384, 132, 84, CUBE_TOP, CUBE_LEFT, CUBE_RIGHT, CUBE_EDGE, 6)
    with_shadow(img, cube_layer, (4, 10), 8, 110)

    # Upload arrow from the crate's top towards the depot cube.
    arrow_layer = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    arrow(arrow_layer, (236, 236), (338, 150), 34, 96, 70, WHITE)
    with_shadow(img, arrow_layer, (4, 8), 6, 120)

    mod_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    # Resources/ ships inside the mod (PluginSettings.ini), so only the 128 px icon goes there.
    resources_dir = os.path.join(mod_dir, "Resources")
    branding_dir = os.path.join(mod_dir, "Branding")
    os.makedirs(resources_dir, exist_ok=True)
    os.makedirs(branding_dir, exist_ok=True)
    big = img.resize((BASE, BASE), Image.LANCZOS)
    big.save(os.path.join(branding_dir, "Icon512.png"))
    big.resize((128, 128), Image.LANCZOS).save(os.path.join(resources_dir, "Icon128.png"))
    print("wrote", resources_dir, "and", branding_dir)


main()
