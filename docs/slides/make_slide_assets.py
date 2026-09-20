"""Diagrams for the "How it works" slides, in the deck's palette, on transparent backgrounds
so they sit on the template's cream slide under the yellow title bar. Run: tracker/.venv/bin/python docs/slides/make_slide_assets.py"""
import math, os
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.dirname(os.path.abspath(__file__))
CREAM, YELLOW, DARK, RUST, ORANGE, SAND, RED, WHITE = "#F6EEE6", "#F2D063", "#48251F", "#72361D", "#D98E4D", "#E9DDD2", "#C0392B", "#FFFFFF"
W, H = 2400, 1080

def font(size, kind="head"):
    path = {"head": "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf", "body": "/System/Library/Fonts/Avenir Next.ttc"}[kind]
    return ImageFont.truetype(path, size, index=2 if kind == "body" else 0)      # Avenir Next index 2 = Demi Bold

def canvas():
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)

def text(d, xy, s, size, fill=DARK, kind="head", anchor="mm"):
    d.multiline_text(xy, s, font=font(size, kind), fill=fill, anchor=anchor, align="center", spacing=size * 0.18)

def box(d, x, y, w, h, fill=YELLOW, outline=None, r=26, width=6):
    d.rounded_rectangle((x, y, x + w, y + h), r, fill=fill, outline=outline, width=width)

def arrow(d, a, b, fill=RUST, width=14, head=44, dash=False):
    ax, ay = a; bx, by = b
    ang = math.atan2(by - ay, bx - ax)
    ex, ey = bx - math.cos(ang) * head * 0.8, by - math.sin(ang) * head * 0.8
    if dash:
        n = int(math.hypot(ex - ax, ey - ay) // 46)
        for i in range(n):
            t0, t1 = i / n, (i + 0.55) / n
            d.line((ax + (ex - ax) * t0, ay + (ey - ay) * t0, ax + (ex - ax) * t1, ay + (ey - ay) * t1), fill=fill, width=width)
    else:
        d.line((ax, ay, ex, ey), fill=fill, width=width)
    d.polygon([(bx, by), (bx - math.cos(ang - 0.45) * head, by - math.sin(ang - 0.45) * head), (bx - math.cos(ang + 0.45) * head, by - math.sin(ang + 0.45) * head)], fill=fill)

def badge(d, cx, cy, n):
    d.ellipse((cx - 46, cy - 46, cx + 46, cy + 46), fill=DARK)
    text(d, (cx, cy + 6), str(n), 64, YELLOW)

# ------------------------------------------------------------------ 1. pipeline
im, d = canvas()
steps = [("WEBCAM", "30 frames\na second"), ("HAND + BODY\nPOINTS", "MediaPipe\n21 + 33 landmarks"), ("OUR TRACKER", "Python: aim, trigger,\nreload, lean, duck"), ("THE GAME", "Unreal Engine 5\nC++, 60 fps")]
bw, bh, gap, y0 = 470, 300, 140, 150
x = (W - (bw * 4 + gap * 3)) // 2
centres = []
for i, (title, sub) in enumerate(steps):
    box(d, x, y0, bw, bh, fill=DARK if i == 2 else YELLOW)
    text(d, (x + bw / 2, y0 + bh / 2 + 8), title, 86, YELLOW if i == 2 else DARK)
    text(d, (x + bw / 2, y0 + bh + 95), sub, 44, RUST, "body")
    badge(d, x + 30, y0 + 20, i + 1)
    centres.append((x, x + bw))
    if i < 3:
        arrow(d, (x + bw + 16, y0 + bh / 2), (x + bw + gap - 16, y0 + bh / 2))
    x += bw + gap
text(d, ((centres[2][1] + centres[3][0]) / 2, y0 + bh / 2 - 70), "UDP", 40, RUST, "body")
text(d, ((centres[2][1] + centres[3][0]) / 2, y0 + bh / 2 + 70), "30/s", 40, RUST, "body")
gx = centres[2][0] + bw / 2
box(d, gx - 260, 790, 520, 170, fill=ORANGE)
text(d, (gx, 880), "ARDUINO GLOVE", 70, DARK)
arrow(d, (gx - 70, 640), (gx - 70, 780)); arrow(d, (gx + 70, 780), (gx + 70, 640))
text(d, (gx - 640, 875), "real trigger switch in,\nbuzz out on every shot and hit", 42, RUST, "body")
text(d, (W / 2, 1030), "THE PART WE BUILT IS THE DARK BOX: TURNING WOBBLY DOTS INTO A GUN", 58, DARK)
im.save(os.path.join(OUT, "1_pipeline.png"))

# ------------------------------------------------------------------ 2. aim
im, d = canvas()
def person(cx, cy, hand, ghost=None):
    d.ellipse((cx - 60, cy - 250, cx + 60, cy - 130), outline=DARK, width=12)                 # head
    d.line((cx - 170, cy - 90, cx + 170, cy - 90), fill=DARK, width=14)                         # shoulders
    d.line((cx, cy - 90, cx, cy + 210), fill=DARK, width=12)
    d.ellipse((cx - 18, cy - 108, cx + 18, cy - 72), fill=ORANGE)                                # chest anchor
    for h, col in ((ghost, SAND), (hand, DARK)):
        if h is None: continue
        d.line((cx + 170, cy - 90, h[0], h[1]), fill=col, width=12)
        d.ellipse((h[0] - 34, h[1] - 34, h[0] + 34, h[1] + 34), fill=col)
# left: why direction fails
box(d, 60, 60, 1020, 960, fill=SAND, r=40)
text(d, (570, 140), "POINTING AT THE CAMERA", 76)
person(570, 560, (640, 470))
d.ellipse((640 - 74, 470 - 74, 640 + 74, 470 + 74), outline=RED, width=10)
text(d, (570, 860), "the finger is a dot: its direction is lost\n(tried it: tens of cm off, jittery)", 44, RUST, "body")
d.line((880, 330, 1000, 450), fill=RED, width=20); d.line((1000, 330, 880, 450), fill=RED, width=20)
# right: what we do
box(d, 1320, 60, 1020, 960, fill=YELLOW, r=40)
text(d, (1830, 140), "SO WE TRACK TRAVEL", 76)
person(1760, 560, (2080, 380), ghost=(1900, 520))
arrow(d, (1915, 505), (2060, 395), fill=RED, width=12, head=36)
text(d, (2130, 300), "finger\ntravel", 40, RED, "body")
arrow(d, (1600, 470), (1720, 470), fill=ORANGE, width=12, head=36)
text(d, (1540, 410), "minus\nbody travel", 40, RUST, "body")
text(d, (1830, 860), "fingertip position is steady to 2 mm\ndodging does not drag the crosshair", 44, RUST, "body")
arrow(d, (1100, 540), (1300, 540), width=20, head=60)
im.save(os.path.join(OUT, "2_aim_travel_not_direction.png"))

# ------------------------------------------------------------------ 3. trigger rewind
im, d = canvas()
x0, x1, base = 160, 2240, 560
d.line((x0, 900, x1, 900), fill=DARK, width=8); text(d, (x1 - 40, 950), "time", 40, RUST, "body")
pts = []
for i in range(0, 1001):
    t = i / 1000
    x = x0 + (x1 - x0) * t
    y = base + math.sin(t * 40) * 6
    if t > 0.60: y += -260 * math.exp(-((t - 0.70) / 0.06) ** 2) + 40 * math.sin((t - 0.6) * 60) * math.exp(-(t - 0.6) * 9)
    pts.append((x, y))
fx = x0 + (x1 - x0) * 0.60
d.rectangle((fx, 170, fx + 380, 900), fill="#F2D06355")
d.line(pts, fill=DARK, width=12, joint="curve")
text(d, (x0 + 360, base - 90), "where you are aiming", 46, DARK, "body")
text(d, (fx + 190, 120), "THUMB DROPS\nhand jerks", 60, RUST)
sx = x0 + (x1 - x0) * 0.50
d.ellipse((sx - 34, base - 34, sx + 34, base + 34), fill=RED)
arrow(d, (fx + 150, 760), (sx + 30, 760), fill=RED, width=14, head=48)
text(d, (fx - 40, 815), "REWIND", 64, RED)
arrow(d, (sx, 380), (sx, base - 50), fill=RED, width=10, head=36)
text(d, (sx, 320), "THE SHOT FIRES FROM HERE", 70, RED)
text(d, (W / 2, 1020), "A TRIGGER THAT DOESN'T THROW THE SHOT · AIM HISTORY, REWOUND TO BEFORE THE GESTURE", 54, DARK)
im.save(os.path.join(OUT, "3_trigger_rewind.png"))

# ------------------------------------------------------------------ 4. the world moves
im, d = canvas()
def pose(s):                      # a gentle S curve, top-down. s in chunks.
    x = 170 + s * 228
    y = 520 + 150 * math.sin((s - 3.2) * 0.62)
    ang = math.atan2(150 * 0.62 * math.cos((s - 3.2) * 0.62), 235)
    return x, y, ang
def tile(s, fill, outline, dash=False, label=None):
    x, y, a = pose(s + 0.5)
    L, Wd = 112, 170
    c, sn = math.cos(a), math.sin(a)
    corners = [(x + c * dx - sn * dy, y + sn * dx + c * dy) for dx, dy in ((-L, -Wd), (L, -Wd), (L, Wd), (-L, Wd))]
    d.polygon(corners, fill=fill, outline=outline, width=6)
    if label: text(d, (x, y - Wd - 40), label, 40, RUST, "body")
for s in range(0, 9):
    behind, ahead = s < 2, s > 6
    tile(s, "#E9DDD2AA" if behind else (None if ahead else SAND), RUST if not behind else "#B9A89A")
for k in range(0, 900):                                   # the track
    s = k / 100
    x, y, a = pose(s); x2, y2, _ = pose(s + 0.012)
    d.line((x, y, x2, y2), fill=DARK, width=10)
tx, ty, ta = pose(4.0)
for off in (-1.1, -0.55, 0, 0.55):                        # train cars sit on the track
    x, y, a = pose(4.0 + off)
    car = Image.new("RGBA", (130, 64), (0, 0, 0, 0)); ImageDraw.Draw(car).rounded_rectangle((0, 0, 129, 63), 14, fill=RED if off == 0 else RUST)
    car = car.rotate(-math.degrees(a), expand=True, resample=Image.BICUBIC)
    im.alpha_composite(car, (int(x - car.width / 2), int(y - car.height / 2)))
text(d, (W / 2, 110), "THE TRAIN NEVER MOVES", 110, DARK)
text(d, (W / 2, 205), "the world slides and turns under it", 46, RUST, "body")
arrow(d, (1500, 900), (900, 900), fill=ORANGE, width=16, head=54)
text(d, (1200, 850), "the world moves this way", 40, RUST, "body")
x, y, _ = pose(0.9); text(d, (x + 40, y + 290), "DROPPED\n70 m behind", 48, RUST)
d.line((x - 70, y - 60, x + 70, y + 60), fill=RED, width=16); d.line((x + 70, y - 60, x - 70, y + 60), fill=RED, width=16)
x, y, _ = pose(8.0); text(d, (x - 60, y + 300), "LAID\n450 m ahead", 48, RUST)
text(d, (W / 2, 1010), "38 chunks · 4 joint types · chunk B may follow A when their joints match", 44, RUST, "body")
im.save(os.path.join(OUT, "4_world_moves_under_the_train.png"))

# ------------------------------------------------------------------ 5. fair fight timeline
im, d = canvas()
y = 430
segs = [(200, 900, RED, "0.7 s  RED FLASH AT THE BARREL", "telegraph: HUD ring closes in"), (900, 1080, DARK, "BANG", ""), (1080, 1900, ORANGE, "BULLET IN FLIGHT  0.7 s", "aimed at where your head WAS")]
for a, b, col, title, sub in segs:
    box(d, a, y, b - a - 14, 170, fill=col, r=20)
    text(d, ((a + b) / 2, y + 90), title, 60 if b - a > 300 else 56, WHITE if col != ORANGE else DARK)
    if sub: text(d, ((a + b) / 2, y + 230), sub, 42, RUST, "body")
arrow(d, (1910, y + 85), (2050, y + 85))
box(d, 2060, y - 40, 300, 250, fill=YELLOW); text(d, (2210, y + 90), "LEAN\n= MISS", 74)
text(d, (W / 2, 180), "EVERY SHOT IS DODGEABLE", 110)
for i, s in enumerate(("max 2 bandits shooting at once", "nobody shoots from off screen", "7° aim assist: generous wins")):
    cx = 420 + i * 780
    box(d, cx - 350, 800, 700, 130, fill=SAND, r=20); text(d, (cx, 868), s, 40, DARK, "body")
im.save(os.path.join(OUT, "5_every_shot_is_dodgeable.png"))

# ------------------------------------------------------------------ 6. stat badges, one file each
for name, big, small in (("fps", "36 TO 60 FPS", "Lumen + ray tracing off, every asset preloaded"), ("span", "46 CM", "of hand travel crosses the whole screen"),
                         ("assist", "7°", "aim assist: webcam aim is noisy"), ("tests", "55 TESTS", "plus every play session recorded and replayed"),
                         ("chunks", "38 CHUNKS", "stitched into an endless line")):
    b = Image.new("RGBA", (900, 330), (0, 0, 0, 0)); bd = ImageDraw.Draw(b)
    bd.rounded_rectangle((0, 0, 899, 210), 24, fill=YELLOW)
    bd.text((450, 118), big, font=font(150), fill=DARK, anchor="mm")
    bd.text((450, 275), small, font=font(40, "body"), fill=RUST, anchor="mm")
    b.save(os.path.join(OUT, f"stat_{name}.png"))
print("done", sorted(f for f in os.listdir(OUT) if f.endswith(".png")))
