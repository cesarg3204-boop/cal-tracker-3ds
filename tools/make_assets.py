#!/usr/bin/env python3
"""Draws the HOME menu art for Cal Pop: one elbow macaroni on the
green -> turquoise -> blue gradient, plus the short banner jingle.

Writes icon.png (48x48), meta/banner.png (256x128) and meta/banner.wav.
Needs Pillow. Run from the repo root: python3 tools/make_assets.py
"""
import math
import struct
import wave

from PIL import Image, ImageDraw, ImageFilter

GREEN = (0x22, 0xC5, 0x5E)
TURQ = (0x2D, 0xD4, 0xBF)
BLUE = (0x3B, 0x82, 0xF6)

# macaroni colors: shadow side, body, light side, shine, inside of the tube
MAC_DARK = (0xD9, 0x8A, 0x0B)
MAC_BODY = (0xF5, 0xB3, 0x1C)
MAC_LIGHT = (0xFF, 0xD5, 0x4F)
MAC_SHINE = (0xFF, 0xF4, 0xC4)
MAC_HOLE = (0x9A, 0x58, 0x05)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def grad(t):
    t = max(0.0, min(1.0, t))
    return lerp(GREEN, TURQ, t * 2) if t < 0.5 else lerp(TURQ, BLUE, (t - 0.5) * 2)


def background(w, h):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            # diagonal gradient, top-left green to bottom-right blue
            px[x, y] = grad((x / w) * 0.75 + (y / h) * 0.25)
    # soft light spot behind the noodle
    glow = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(glow)
    d.ellipse([w * 0.18, h * 0.05, w * 0.82, h * 0.95], fill=90)
    glow = glow.filter(ImageFilter.GaussianBlur(min(w, h) * 0.18))
    img = Image.composite(Image.new("RGB", (w, h), (255, 255, 255)), img, glow)
    return img


def macaroni(img, cx, cy, radius, tube, a0, a1):
    """Draws one elbow macaroni: a tube bent along an arc from angle a0 to a1
    (degrees, around cx, cy), with the open end at a1."""
    w, h = img.size
    steps = 240
    pts = []
    for i in range(steps + 1):
        a = math.radians(a0 + (a1 - a0) * i / steps)
        pts.append((cx + radius * math.cos(a), cy + radius * math.sin(a)))

    # drop shadow
    shadow = Image.new("L", (w, h), 0)
    sd = ImageDraw.Draw(shadow)
    off = tube * 0.45
    for x, y in pts:
        sd.ellipse([x - tube + off, y - tube + off * 1.4, x + tube + off, y + tube + off * 1.4], fill=110)
    shadow = shadow.filter(ImageFilter.GaussianBlur(tube * 0.5))
    img.paste((10, 40, 60), (0, 0), shadow)

    d = ImageDraw.Draw(img)
    # light from the top left: each pass shifts its circles toward the light
    passes = [
        (MAC_DARK, 1.00, 0.00),
        (MAC_BODY, 0.86, 0.12),
        (MAC_LIGHT, 0.58, 0.30),
        (MAC_SHINE, 0.20, 0.52),
    ]
    for color, scale, shift in passes:
        r = tube * scale
        for x, y in pts:
            sx, sy = x - tube * shift * 0.7, y - tube * shift
            d.ellipse([sx - r, sy - r, sx + r, sy + r], fill=color)

    # the open end: an ellipse across the tube, facing along the arc
    a = math.radians(a1)
    ex, ey = pts[-1]
    rim_w, rim_h = tube * 0.55, tube * 1.02  # thin along the arc, wide across it
    cap = Image.new("RGBA", (int(tube * 4), int(tube * 4)), (0, 0, 0, 0))
    cd = ImageDraw.Draw(cap)
    c = tube * 2
    cd.ellipse([c - rim_w, c - rim_h, c + rim_w, c + rim_h], fill=MAC_LIGHT + (255,))
    cd.ellipse([c - rim_w * 0.62, c - rim_h * 0.68, c + rim_w * 0.62, c + rim_h * 0.68], fill=MAC_HOLE + (255,))
    # tangent direction of the arc at the end
    tangent = math.degrees(a) + 90
    cap = cap.rotate(-tangent, resample=Image.BICUBIC, center=(c, c))
    img.paste(cap, (int(ex - c), int(ey - c)), cap)


def draw(w, h, scale=4):
    W, H = w * scale, h * scale
    img = background(W, H)
    s = min(W, H)
    macaroni(img, W * 0.5, H * 0.64, s * 0.30, s * 0.13, 195, 345)
    return img.resize((w, h), Image.LANCZOS)


def rounded(img, radius):
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, img.size[0] - 1, img.size[1] - 1], radius, fill=255)
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.paste(img, (0, 0), mask)
    return out


def jingle(path, rate=32000):
    """A short "pop" followed by a bright three-note chime (about 1.5 s)."""
    n = int(rate * 1.5)
    out = [0.0] * n

    def tone(start, dur, freq, vol, sweep=0.0):
        s0 = int(start * rate)
        phase = 0.0
        for i in range(int(dur * rate)):
            if s0 + i >= n:
                break
            t = i / rate
            f = freq * (1 + sweep * math.exp(-t * 40))
            phase += 2 * math.pi * f / rate
            env = min(1.0, t * 400) * math.exp(-t * 6.0)
            v = math.sin(phase) + 0.3 * math.sin(2 * phase) + 0.1 * math.sin(3 * phase)
            out[s0 + i] += v * env * vol

    tone(0.00, 0.12, 380, 0.55, sweep=1.6)  # pop
    tone(0.14, 0.6, 659.25, 0.30)           # E5
    tone(0.26, 0.6, 783.99, 0.30)           # G5
    tone(0.38, 1.1, 1046.50, 0.34)          # C6
    tone(0.38, 1.1, 1318.51, 0.12)          # E6 sparkle
    peak = max(abs(v) for v in out) or 1
    frames = b"".join(struct.pack("<h", int(v / peak * 0.8 * 32767)) for v in out)
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        wf.writeframes(frames)


if __name__ == "__main__":
    draw(256, 128).save("meta/banner.png")
    rounded(draw(48, 48, scale=8), 8).save("icon.png")
    jingle("meta/banner.wav")
    print("wrote meta/banner.png, icon.png, meta/banner.wav")
