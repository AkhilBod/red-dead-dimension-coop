"""Track, tileable desert terrain, rock formations and plants. The line runs along Y; the train heads -Y.

Everything is seeded, so the same call always gives the same mesh. Tiles are TILE_LEN long and wrap seamlessly in Y.
"""
import random
from math import cos, pi, radians, sin

from mathutils import Euler, Vector, noise

from .core import R, cyl

TRACK_LEN = 20.0
TILE_LEN = 60.0
TILE_W = 260.0
RAIL_TOP = 0.53
GAUGE = 0.78
SECOND_TRACK_X = 7.0     # the bandit train runs here
TIE_STEP = 0.625


# ---------------------------------------------------------------- track

def rails(mb, length, z=0.36):
    for sx in (1, -1):
        x = sx * GAUGE
        mb.box((x, length / 2, z + 0.015), (0.15, length, 0.03), "steel_dark")
        mb.box((x, length / 2, z + 0.075), (0.045, length, 0.09), "steel_dark")
        mb.box((x, length / 2, z + 0.145), (0.080, length, 0.05), "steel")


def track_straight(mb, length=TRACK_LEN, ballast=True, seed=1):
    """Origin at the start of the piece, centred on the rails, ground at z=0. Rail top is RAIL_TOP."""
    rng = random.Random(seed)
    if ballast:
        v = [(-2.7, 0, 0), (-1.75, 0, 0.25), (1.75, 0, 0.25), (2.7, 0, 0), (-2.7, length, 0), (-1.75, length, 0.25), (1.75, length, 0.25),
             (2.7, length, 0)]
        mb.poly(v, [(0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6)], "gravel")
    n = int(round(length / TIE_STEP))
    for k in range(n):
        y = (k + 0.5) * length / n
        mb.box((rng.uniform(-0.04, 0.04), y, 0.29), (2.55 + rng.uniform(-0.08, 0.08), 0.24, 0.14), rng.choice(("plank_dark", "plank_dark", "deadwood")),
               rot=Euler((0, 0, rng.uniform(-0.025, 0.025))))
        for sx in (1, -1):
            mb.box((sx * GAUGE, y, 0.372), (0.26, 0.16, 0.02), "iron")
    rails(mb, length)


def track_trestle(mb, length=TRACK_LEN, height=9.0):
    """Timber trestle for crossing a gulch. Deck at z=0 (put it level with normal ground), legs go down."""
    n = int(round(length / TIE_STEP))
    for k in range(n):
        mb.box((0, (k + 0.5) * length / n, 0.29), (3.4, 0.24, 0.14), "plank_dark")
    rails(mb, length)
    for sx in (1, -1):
        mb.box((sx * 0.9, length / 2, 0.08), (0.30, length, 0.34), "plank")
        mb.box((sx * 1.62, length / 2, 0.42), (0.12, length, 0.12), "plank_light")
    for y in (2.5, 7.5, 12.5, 17.5):
        if y > length:
            break
        mb.box((0, y, -0.25), (4.0, 0.34, 0.34), "plank")
        for sx, lean in ((-1, -0.22), (-0.35, -0.05), (0.35, 0.05), (1, 0.22)):
            top = Vector((sx * 1.6, y, -0.4))
            cyl(mb, top, top + Vector((lean * height, 0, -height)), 0.17, "deadwood", n=6)
        for lvl in range(1, int(height / 3) + 1):
            z = -0.4 - lvl * 3.0 + 1.2
            w = 3.4 + 0.44 * (lvl * 3.0 - 1.2)
            mb.box((0, y, z), (w, 0.14, 0.26), "plank_dark")
            mb.box((0, y + 0.1, z + 1.4), (w * 1.02, 0.10, 0.22), "plank_dark", rot=Euler((0, radians(38), 0)))


def buffer_stop(mb):
    for sx in (1, -1):
        mb.box((sx * GAUGE, 0.0, 0.95), (0.22, 0.22, 1.1), "plank_dark")
        mb.box((sx * GAUGE, 0.75, 0.80), (0.18, 1.7, 0.18), "plank_dark", rot=Euler((radians(-38), 0, 0)))
    mb.box((0, -0.12, 1.25), (2.5, 0.26, 0.36), "paint_red")
    mb.box((0, -0.26, 1.25), (2.5, 0.03, 0.12), "paint_white")


def telegraph_span(mb, span=30.0, seed=2):
    """One pole plus the wires reaching the next pole `span` metres further along +Y. Instance every `span`."""
    rng = random.Random(seed)
    cyl(mb, (0, 0, -0.3), (0.05, 0, 7.2), 0.13, "deadwood", n=6, r2=0.09)
    for z, w in ((6.7, 2.0), (6.1, 1.5)):
        mb.box((0.04, 0, z), (w, 0.12, 0.12), "plank_dark")
        for x in (-w / 2 + 0.15, -w / 6, w / 6, w / 2 - 0.15):
            cyl(mb, (x + 0.04, 0, z + 0.06), (x + 0.04, 0, z + 0.20), 0.035, "bottle_green", n=6)
            pts = []
            for i in range(9):
                t = i / 8
                pts.append(R((x + 0.04, t * span, z + 0.2 - 0.9 * sin(pi * t)), 0.012))
            mb.loft(pts, "iron", n=3)


# ---------------------------------------------------------------- terrain

def _h(x, y, seed, length=TILE_LEN):
    """Height field. Every tile shares the same profile along its y edges, so any tile order joins without gaps."""
    e = min(y, length - y) / 12.0
    if seed == 0 or e >= 1.0:
        return _h_raw(x, y, seed, length)
    e = max(0.0, e)
    e = e * e * (3 - 2 * e)
    return _h_raw(x, 0.0, 0, length) * (1 - e) + _h_raw(x, y, seed, length) * e


def _h_raw(x, y, seed, length=TILE_LEN):
    a = 2 * pi * y / length
    rad = length / (2 * pi)
    p = Vector((x * 0.035 + seed * 7.3, rad * cos(a) * 0.035, rad * sin(a) * 0.035))
    q = Vector((x * 0.11 + seed * 3.1, rad * cos(a) * 0.11, rad * sin(a) * 0.11))
    big = noise.noise(p)
    small = noise.noise(q)
    d = abs(x - SECOND_TRACK_X / 2)
    flat = max(0.0, min(1.0, (d - 9.0) / 22.0))
    flat = flat * flat * (3 - 2 * flat)
    far = max(0.0, (d - 55.0) / 75.0)
    return flat * (1.6 * big + 0.45 * small + 0.5) + far * far * (9.0 + 8.0 * big) + 0.05 * small


def terrain_tile(mb, seed=1, second_track_bed=True):
    """Origin at the start of the tile, x=0 on the main line. Both track corridors are flat at z=0."""
    xs = [-TILE_W / 2]
    while xs[-1] < TILE_W / 2:
        d = abs(xs[-1] - SECOND_TRACK_X / 2)
        xs.append(xs[-1] + (2.5 if d < 14 else 5.0 if d < 45 else 10.0))
    ny = 20
    ys = [TILE_LEN * j / ny for j in range(ny + 1)]
    rng = random.Random(seed)
    grid = []
    for i, x in enumerate(xs):
        row = []
        for j, y in enumerate(ys):
            jx = 0 if (j in (0, ny) or abs(x - SECOND_TRACK_X / 2) < 9) else rng.uniform(-0.8, 0.8)
            row.append(mb.vert((x + jx, y, _h(x, y, seed))))
        grid.append(row)
    for i in range(len(xs) - 1):
        for j in range(ny):
            x, y = (xs[i] + xs[i + 1]) / 2, (ys[j] + ys[j + 1]) / 2
            t = noise.noise(Vector((x * 0.05, (y % TILE_LEN) * 0.05 + seed, seed * 1.7)))
            h = _h(x, y, seed)
            on_bed = abs(x) < 3.6 or (second_track_bed and abs(x - SECOND_TRACK_X) < 3.6)
            mat = "dirt" if on_bed else ("sand_dark" if (t < -0.18 or h > 6) else "sand_light" if t > 0.22 else "sand")
            if h > 11:
                mat = "rock_pale"
            a, b, c, d = grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]
            if (i + j) % 2:
                mb.face([a, b, c], mat)
                mb.face([a, c, d], mat)
            else:
                mb.face([a, b, d], mat)
                mb.face([b, c, d], mat)


# ---------------------------------------------------------------- rocks

def _jit(rng, amt):
    cache = {}

    def f(a):
        k = round(a, 3)
        if k not in cache:
            cache[k] = 1.0 + rng.uniform(-amt, amt)
        return cache[k]
    return f


def mesa(mb, seed=1, width=55.0, depth=38.0, height=34.0, skirt=1.7):
    rng = random.Random(seed)
    n = 11
    rot0 = rng.uniform(0, 1)
    strata = ["rock_dark", "rock_red", "rock_orange", "rock_red", "rock_pale", "rock_orange"]
    rings = [R((0, 0, -1.0), width / 2 * skirt, depth / 2 * skirt, rfn=_jit(rng, 0.10), mat="sand_dark")]
    rings.append(R((0, 0, height * 0.22), width / 2 * 1.22, depth / 2 * 1.22, rfn=_jit(rng, 0.10), mat="rock_dark"))
    rings.append(R((0, 0, height * 0.30), width / 2 * 1.02, depth / 2 * 1.02, rfn=_jit(rng, 0.10), mat=strata[1]))
    z = height * 0.30
    k = 2
    while z < height * 0.96:
        dz = height * rng.uniform(0.10, 0.18)
        z = min(height, z + dz)
        shrink = 1.0 - 0.10 * (z / height) + rng.uniform(-0.03, 0.03)
        rings.append(R((rng.uniform(-1, 1), rng.uniform(-1, 1), z), width / 2 * shrink, depth / 2 * shrink, rfn=_jit(rng, 0.09),
                       mat=strata[k % len(strata)]))
        k += 1
        if rng.random() < 0.5 and z < height * 0.9:   # ledge
            rings.append(R((0, 0, z + 0.2), width / 2 * shrink * 0.93, depth / 2 * shrink * 0.93, rfn=_jit(rng, 0.09), mat=strata[k % len(strata)]))
    top = rings[-1]
    rings.append(R(top["c"] + Vector((0, 0, 0.8)), top["rx"] * 0.80, top["ry"] * 0.80, rfn=_jit(rng, 0.12), mat="rock_pale"))
    mb.loft(rings, "rock_red", n=n, p=2.6, caps=(False, True))


def butte(mb, seed=1, r=7.0, height=30.0):
    mesa(mb, seed=seed, width=r * 2, depth=r * 1.7, height=height, skirt=2.6)


def rock(mb, seed=1, s=1.0, c=(0, 0, 0), mat=None):
    rng = random.Random(seed)
    c = Vector(c)
    m = mat or rng.choice(("rock_red", "rock_orange", "rock_grey", "rock_dark"))
    j = _jit(rng, 0.22)
    mb.loft([R(c + Vector((0, 0, -0.1 * s)), 0.9 * s, 0.7 * s, rfn=j), R(c + Vector((rng.uniform(-.1, .1) * s, 0, 0.35 * s)), 1.0 * s, 0.8 * s, rfn=_jit(rng, 0.2)),
             R(c + Vector((rng.uniform(-.2, .2) * s, rng.uniform(-.2, .2) * s, 0.8 * s)), 0.6 * s, 0.5 * s, rfn=_jit(rng, 0.25)),
             R(c + Vector((0, 0, 1.0 * s)), 0.15 * s, 0.12 * s)], m, n=7, p=2.4)


def rock_cluster(mb, seed=1, s=1.0):
    rng = random.Random(seed)
    rock(mb, seed, 1.6 * s)
    for k in range(rng.randint(2, 4)):
        a = rng.uniform(0, 6.28)
        rock(mb, seed * 10 + k, rng.uniform(0.4, 0.9) * s, c=(cos(a) * 1.9 * s, sin(a) * 1.6 * s, 0))


def rock_arch(mb, seed=1, span=16.0, height=13.0):
    rng = random.Random(seed)
    pts = []
    for i in range(10):
        t = i / 9
        a = pi * t
        pts.append(R((-cos(a) * span / 2, rng.uniform(-0.6, 0.6), sin(a) * height - 1.0 * (1 - sin(a))), 2.6 + 1.6 * abs(cos(a)), 2.2 + 1.2 * abs(cos(a)),
                     rfn=_jit(rng, 0.15), mat=("rock_red", "rock_orange")[i % 2]))
    mb.loft(pts, "rock_red", n=7, p=2.5, front=(0, -1, 0))


# ---------------------------------------------------------------- plants and clutter

def saguaro(mb, seed=1, h=4.5):
    rng = random.Random(seed)
    col = rng.choice(("cactus", "cactus_dark"))
    mb.loft([R((0, 0, -0.1), 0.26), R((0, 0, h * 0.5), 0.30), R((0, 0, h - 0.3), 0.27), R((0, 0, h), 0.12)], col, n=7)
    for k in range(rng.randint(1, 3)):
        a = rng.uniform(0, 6.28)
        z = h * rng.uniform(0.35, 0.6)
        d = Vector((cos(a), sin(a), 0))
        out = rng.uniform(0.7, 1.0)
        up = h * rng.uniform(0.25, 0.42)
        mb.loft([R(d * 0.15 + Vector((0, 0, z)), 0.17), R(d * out + Vector((0, 0, z + 0.05)), 0.19, axis=(d + Vector((0, 0, 1)))),
                 R(d * out + Vector((0, 0, z + up)), 0.17), R(d * out + Vector((0, 0, z + up + 0.2)), 0.07)], col, n=6)
    if rng.random() < 0.5:
        mb.box((0, 0, h + 0.05), (0.16, 0.16, 0.10), "flower")


def barrel_cactus(mb, seed=1):
    mb.loft([R((0, 0, -0.05), 0.22), R((0, 0, 0.28), 0.34), R((0, 0, 0.58), 0.26), R((0, 0, 0.66), 0.08)], "cactus", n=8)
    mb.box((0, 0, 0.69), (0.12, 0.12, 0.07), "flower")


def prickly_pear(mb, seed=1):
    rng = random.Random(seed)
    pads = [(Vector((0, 0, 0.30)), 0.0, 0.34)]
    for k in range(rng.randint(3, 5)):
        b, _, s = rng.choice(pads)
        pads.append((b + Vector((rng.uniform(-0.35, 0.35), rng.uniform(-0.15, 0.15), s * 1.3)), rng.uniform(-0.8, 0.8), s * rng.uniform(0.7, 0.95)))
    for c, a, s in pads:
        d = Vector((sin(a) * 0.06, cos(a) * 0.06, 0))
        mb.loft([R(c - d, s * 0.7, s * 0.95), R(c, s, s * 1.2), R(c + d, s * 0.7, s * 0.95)], rng.choice(("cactus", "cactus_dark")), n=7,
                front=(0, 0, 1))


def sagebrush(mb, seed=1, s=1.0):
    rng = random.Random(seed)
    for k in range(rng.randint(3, 5)):
        c = Vector((rng.uniform(-0.4, 0.4) * s, rng.uniform(-0.4, 0.4) * s, 0))
        r = rng.uniform(0.3, 0.55) * s
        mb.loft([R(c, r * 0.4, rfn=_jit(rng, 0.2)), R(c + Vector((0, 0, r * 0.6)), r, rfn=_jit(rng, 0.25)), R(c + Vector((0, 0, r * 1.2)), r * 0.7, rfn=_jit(rng, 0.25)),
                 R(c + Vector((0, 0, r * 1.5)), r * 0.15)], rng.choice(("sage", "sage", "cactus_dark")), n=6)


def dead_tree(mb, seed=1, h=4.0):
    rng = random.Random(seed)

    def branch(p, d, l, r, depth):
        e = p + d * l
        cyl(mb, p, e, r, "deadwood", n=5, r2=r * 0.6)
        if depth:
            for k in range(rng.randint(2, 3)):
                nd = (d + Vector((rng.uniform(-0.9, 0.9), rng.uniform(-0.9, 0.9), rng.uniform(0.1, 0.7)))).normalized()
                branch(p + d * l * rng.uniform(0.55, 1.0), nd, l * rng.uniform(0.5, 0.7), r * 0.55, depth - 1)
    branch(Vector((0, 0, -0.2)), Vector((rng.uniform(-0.1, 0.1), rng.uniform(-0.1, 0.1), 1)).normalized(), h * 0.5, 0.20, 2)


def tumbleweed(mb, seed=1, r=0.45):
    rng = random.Random(seed)
    for k in range(9):
        ax = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))).normalized()
        ring = []
        u = ax.orthogonal().normalized()
        v = ax.cross(u)
        for i in range(8):
            a = 2 * pi * i / 8
            ring.append(R(Vector((0, 0, r)) + (u * cos(a) + v * sin(a)) * r * rng.uniform(0.8, 1.05), 0.012))
        ring.append(ring[0])
        mb.loft(ring, "rope" if k % 2 else "deadwood", n=3, caps=(False, False))


def cow_skull(mb, seed=1):
    mb.loft([R((0, 0.12, 0.10), 0.10, 0.08), R((0, -0.05, 0.10), 0.12, 0.09), R((0, -0.30, 0.07), 0.06, 0.05), R((0, -0.38, 0.06), 0.045, 0.035)],
            "bone", n=6, front=(0, 0, 1))
    for sx in (1, -1):
        mb.loft([R((sx * 0.10, 0.05, 0.14), 0.035), R((sx * 0.30, 0.06, 0.18), 0.028), R((sx * 0.42, 0.0, 0.32), 0.008)], "bone", n=5)
        mb.box((sx * 0.06, -0.06, 0.155), (0.05, 0.06, 0.03), "eye")


def wagon_wheel(mb, r=0.6):
    mb.loft([R((0, -0.04, r), r - 0.06), R((0, -0.04, r), r), R((0, 0.04, r), r), R((0, 0.04, r), r - 0.06)], "plank_dark", n=12, caps=(False, False))
    cyl(mb, (0, -0.09, r), (0, 0.09, r), 0.10, "iron", n=8, front=(0, 0, 1))
    for k in range(8):
        a = 2 * pi * k / 8
        mb.box((sin(a) * r * 0.5, 0, r + cos(a) * r * 0.5), (0.05, 0.05, r * 0.95), "plank", rot=Euler((0, a, 0)))


def fence(mb, length=6.0):
    n = int(length / 2) + 1
    for k in range(n):
        mb.box((k * length / (n - 1), 0, 0.55), (0.14, 0.14, 1.3), "deadwood", rot=Euler((0, 0.03 * (-1) ** k, 0.2 * k)))
    for z in (0.45, 0.95):
        mb.box((length / 2, 0.02, z), (length + 0.3, 0.06, 0.14), "plank_grey", rot=Euler((0, 0.012 * (1 if z < 0.5 else -1), 0)))


SCATTER = {
    "Saguaro_A": (saguaro, dict(seed=1, h=4.6)), "Saguaro_B": (saguaro, dict(seed=5, h=3.4)), "Saguaro_C": (saguaro, dict(seed=9, h=5.6)),
    "BarrelCactus": (barrel_cactus, {}), "PricklyPear_A": (prickly_pear, dict(seed=2)), "PricklyPear_B": (prickly_pear, dict(seed=6)),
    "Sagebrush_A": (sagebrush, dict(seed=1)), "Sagebrush_B": (sagebrush, dict(seed=4, s=1.4)), "DeadTree_A": (dead_tree, dict(seed=3)),
    "DeadTree_B": (dead_tree, dict(seed=8, h=5.5)), "Rock_A": (rock, dict(seed=1)), "Rock_B": (rock, dict(seed=2, s=1.8)),
    "Rock_C": (rock, dict(seed=5, s=0.6)), "RockCluster_A": (rock_cluster, dict(seed=3)), "RockCluster_B": (rock_cluster, dict(seed=7, s=1.8)),
    "Tumbleweed": (tumbleweed, {}), "CowSkull": (cow_skull, {}), "WagonWheel": (wagon_wheel, {}), "Fence": (fence, {}),
}
LANDMARKS = {
    "Mesa_A": (mesa, dict(seed=1)), "Mesa_B": (mesa, dict(seed=4, width=80, depth=45, height=42)),
    "Mesa_C": (mesa, dict(seed=9, width=38, depth=30, height=26)), "Butte_A": (butte, dict(seed=2)),
    "Butte_B": (butte, dict(seed=6, r=5.0, height=38)), "RockArch": (rock_arch, {}),
}
TRACK = {"Track_Straight_20m": (track_straight, {}), "Track_Trestle_20m": (track_trestle, {}), "Track_BufferStop": (buffer_stop, {}),
         "TelegraphSpan_30m": (telegraph_span, {})}
