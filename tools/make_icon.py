"""Draws icons/driver-momentum.png (512x512) in the Form Guide's look: a dark drawer with its corner cut, a thermal
edge, and the grid's heat as an equaliser from ice (cold, below the line) to magma (hot, above it), with a needle.
Run from the addon folder: python -I tools/make_icon.py"""
import pathlib

from PIL import Image, ImageDraw

S = 4  # drawn at 4x, then scaled down for smooth edges
W = 512 * S

STOPS = [(-1.0, (0x2D, 0x5B, 0xFF)), (-0.5, (0x46, 0xD2, 0xFF)), (0.0, (0x56, 0x60, 0x6C)),
         (0.5, (0xFF, 0xB5, 0x47)), (1.0, (0xFF, 0x5A, 0x1F))]


def thermal(v):
    v = max(-1.0, min(1.0, v))
    for (a, ca), (b, cb) in zip(STOPS, STOPS[1:]):
        if v <= b:
            t = (v - a) / (b - a)
            return tuple(int(ca[i] + (cb[i] - ca[i]) * t) for i in range(3)) + (255,)
    return STOPS[-1][1] + (255,)


def p(*xy):
    return [(x * S, y * S) for x, y in xy]


def main():
    out = pathlib.Path(__file__).resolve().parent.parent / "icons" / "driver-momentum.png"
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))

    # the drawer: a dark square with its top-left corner cut, lit a little from the top
    body = Image.new("RGBA", (W, W))
    bd = ImageDraw.Draw(body)
    top, bottom = (0x16, 0x1A, 0x21), (0x08, 0x0A, 0x0D)
    for y in range(W):
        t = y / (W - 1)
        bd.line([(0, y), (W, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
    mask = Image.new("L", (W, W), 0)
    cut = 96
    ImageDraw.Draw(mask).polygon(p((cut, 0), (512, 0), (512, 512), (0, 512), (0, cut)), fill=255)
    img.paste(body, (0, 0), mask)

    # a faint grid
    grid = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grid)
    for i in range(64, 512, 64):
        gd.line(p((i, 0), (i, 512)), fill=(255, 255, 255, 12), width=S)
        gd.line(p((0, i), (512, i)), fill=(255, 255, 255, 12), width=S)
    gmask = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    gmask.paste(grid, (0, 0), mask)
    img = Image.alpha_composite(img, gmask)
    d = ImageDraw.Draw(img)

    # the thermal edge down the left side, and the magma cut
    for y in range(cut * S, W):
        t = (y - cut * S) / (W - cut * S)
        d.line([(0, y), (14 * S, y)], fill=thermal(1 - 2 * t))
    d.polygon(p((0, cut), (cut, 0), (cut + 20, 0), (0, cut + 20)), fill=thermal(1.0))

    # the equaliser: cold bars below the line on the left, hot bars above it on the right
    mid = 300
    values = [-0.85, -0.6, -0.35, -0.12, 0.15, 0.42, 0.7, 0.95]
    x0, bw, gap = 72, 40, 12
    d.rectangle(p((60, mid - 2), (470, mid + 2)), fill=(0x39, 0x42, 0x4C, 255))
    for i, v in enumerate(values):
        x = x0 + i * (bw + gap)
        h = abs(v) * 190
        col = thermal(v)
        if v >= 0:
            d.rectangle(p((x, mid - h), (x + bw, mid - 6)), fill=col)
            d.rectangle(p((x, mid - h), (x + bw, mid - h + 8)), fill=tuple(min(255, c + 60) for c in col[:3]) + (255,))
        else:
            d.rectangle(p((x, mid + 6), (x + bw, mid + h)), fill=col)

    # the needle on the hottest bar
    nx = x0 + 7 * (bw + gap) + bw / 2
    d.rectangle(p((nx - 4, 64), (nx + 4, 108)), fill=(0xE9, 0xEE, 0xF2, 255))
    d.polygon(p((nx - 16, 52), (nx + 16, 52), (nx, 72)), fill=thermal(1.0))

    img = img.resize((512, 512), Image.LANCZOS)
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out, optimize=True)
    print("written", out, out.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
