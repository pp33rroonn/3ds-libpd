#!/usr/bin/env python3
# Mockups for the mixed-orientation CTRL layout concept.
# 3DS bottom screen = 320x240. Rendered at SCALE for visibility.
# B/W retro aesthetic matching the existing pd_3ds CTRL panel.
from PIL import Image, ImageDraw, ImageFont

SCALE = 3
W, H = 320, 240
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)

def load_font(sz):
    for p in ["/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
              "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf"]:
        try:
            return ImageFont.truetype(p, sz)
        except Exception:
            pass
    return ImageFont.load_default()

F = load_font(8 * SCALE)
FS = load_font(6 * SCALE)

def canvas():
    img = Image.new("RGB", (W * SCALE, H * SCALE), WHITE)
    return img, ImageDraw.Draw(img)

def rect(d, x, y, w, h, fill=None, outline=BLACK, width=1):
    d.rectangle([x*SCALE, y*SCALE, (x+w)*SCALE, (y+h)*SCALE],
                fill=fill, outline=outline, width=max(1, width*SCALE//2 or 1))

def text(d, x, y, s, font=F, fill=BLACK):
    d.text((x*SCALE, y*SCALE), s, font=font, fill=fill)

def hbar(d, x, y, w, h, frac, label, val):
    rect(d, x, y, w, h, fill=WHITE, outline=BLACK)
    kw = max(2, int(w*frac))
    rect(d, x, y, kw, h, fill=BLACK, outline=BLACK)
    text(d, x+w+3, y+(h-9)//2, f"{label} {val}", font=FS)

def vbar(d, x, y, w, h, frac, label, val):
    rect(d, x, y, w, h, fill=WHITE, outline=BLACK)
    kh = max(2, int(h*frac))
    rect(d, x, y+h-kh, w, kh, fill=BLACK, outline=BLACK)
    text(d, x, y+h+2, label, font=FS)
    text(d, x, y+h+11, str(val), font=FS)

# MOCK A: HORIZONTAL layout -> scroll UP/DOWN. Stacked rows.
imgA, dA = canvas()
text(dA, 4, 1, "HORIZONTAL  (scroll up/down)", font=FS)
rowH = 34
y = 12
rows = [("hsl","freq",0.62,220),("vsl","cut",0.40,800),("hsl","vol",0.75,90),
        ("hsl","res",0.30,12),("vsl","env",0.55,64)]
for kind,lab,fr,v in rows:
    if kind=="hsl":
        hbar(dA, 8, y+8, 210, 14, fr, lab, v)
    else:
        vbar(dA, 8, y+2, 14, 26, fr, '', '')
        text(dA, 30, y+6, f"{lab}  {v}", font=FS)
        text(dA, 30, y+16, "(vertical fader, drawn standing)", font=FS, fill=(120,120,120))
    dA.line([0, (y+rowH-2)*SCALE, W*SCALE, (y+rowH-2)*SCALE], fill=(180,180,180), width=1)
    y += rowH
imgA.save("design_assets/mock_horiz.png")

# MOCK B: VERTICAL layout -> scroll LEFT/RIGHT. Side-by-side columns.
imgB, dB = canvas()
text(dB, 4, 1, "VERTICAL  (scroll left/right)", font=FS)
colW = 52
x = 8
cols = [("vsl","freq",0.62,220),("vsl","cut",0.40,800),("hsl","vol",0.75,90),
        ("vsl","res",0.30,12),("hsl","env",0.55,64),("vsl","lfo",0.5,50)]
for kind,lab,fr,v in cols:
    if kind=="vsl":
        vbar(dB, x+6, 14, 16, 180, fr, lab, v)
    else:
        text(dB, x+2, 14, lab, font=FS)
        hbar(dB, x+2, 26, 40, 12, fr, "", v)
        text(dB, x+2, 42, str(v), font=FS)
        text(dB, x+2, 54, "(horiz.)", font=FS)
    dB.line([(x+colW-4)*SCALE, 12*SCALE, (x+colW-4)*SCALE, (H-4)*SCALE], fill=(180,180,180), width=1)
    x += colW
imgB.save("design_assets/mock_vert.png")

# MOCK C: SIMPLER alternative - uniform bars, only scroll differs.
imgC, dC = canvas()
text(dC, 4, 1, "SIMPLE: uniform bars, only scroll follows layout", font=FS)
y = 12
simple = [("freq",0.62,220,"hsl"),("cut",0.40,800,"vsl -> drawn flat"),
          ("vol",0.75,90,"hsl"),("res",0.30,12,"vsl -> drawn flat"),("env",0.55,64,"hsl")]
for lab,fr,v,note in simple:
    hbar(dC, 8, y+8, 200, 14, fr, lab, v)
    text(dC, 222, y+22, note, font=FS, fill=(120,120,120))
    dC.line([0, (y+rowH-2)*SCALE, W*SCALE, (y+rowH-2)*SCALE], fill=(180,180,180), width=1)
    y += rowH
imgC.save("design_assets/mock_simple.png")

print("saved mock_horiz.png mock_vert.png mock_simple.png")
