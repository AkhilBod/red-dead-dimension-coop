"""Buildings, station, rigged set pieces (water tower, windmill, saloon doors, bell) and clutter props.

Buildings face -Y with their origin at the middle of the front wall on the ground.
"""
import random
from math import cos, pi, radians, sin

from mathutils import Euler, Vector

from .core import MB, R, Poser, build_armature, cyl, gable, get_collection, make_action, rot
from .train import barrel, crate


# ---------------------------------------------------------------- building kit

def window(mb, c, w=0.9, h=1.2, trim="paint_white", lit=False, bars=False, face=-1):
    c = Vector(c)
    mb.box(c + Vector((0, face * 0.03, 0)), (w, 0.06, h), "glass_lit" if lit else "glass")
    for dz in (-h / 2, h / 2):
        mb.box(c + Vector((0, face * 0.06, dz)), (w + 0.16, 0.10, 0.09), trim)
    for dx in (-w / 2, w / 2):
        mb.box(c + Vector((dx, face * 0.06, 0)), (0.09, 0.10, h), trim)
    mb.box(c + Vector((0, face * 0.07, 0)), (0.05, 0.06, h), trim)
    mb.box(c + Vector((0, face * 0.07, 0)), (w, 0.06, 0.05), trim)
    if bars:
        for k in range(5):
            cyl(mb, c + Vector((-w / 2 + (k + 0.5) * w / 5, face * 0.10, -h / 2)), c + Vector((-w / 2 + (k + 0.5) * w / 5, face * 0.10, h / 2)), 0.02, "iron", n=4)


def door(mb, c, w=1.0, h=2.1, mat="plank_dark", trim="paint_white", face=-1):
    c = Vector(c)
    mb.box(c + Vector((0, face * 0.03, h / 2)), (w, 0.08, h), mat)
    mb.box(c + Vector((0, face * 0.06, h + 0.06)), (w + 0.22, 0.12, 0.12), trim)
    for dx in (-w / 2 - 0.05, w / 2 + 0.05):
        mb.box(c + Vector((dx, face * 0.06, h / 2)), (0.10, 0.12, h), trim)
    mb.box(c + Vector((w * 0.34, face * 0.09, h * 0.48)), (0.06, 0.06, 0.06), "brass")


def siding(mb, x0, x1, y, z0, z1, mat="plank_dark", step=0.42, face=-1):
    z = z0 + step
    while z < z1 - 0.05:
        mb.box(((x0 + x1) / 2, y + face * 0.012, z), (x1 - x0, 0.03, 0.035), mat)
        z += step


def porch(mb, w, depth, h, posts=4, floor="plank", roof="shingle", post="paint_white", rail=False, z0=0.0):
    mb.box((0, -depth / 2, z0 + 0.15), (w, depth, 0.30), floor)
    mb.box((0, -depth - 0.25, z0 + 0.08), (w * 0.4, 0.5, 0.16), floor)
    for k in range(posts):
        x = -w / 2 + 0.15 + k * (w - 0.3) / (posts - 1)
        mb.box((x, -depth + 0.15, z0 + 0.3 + h / 2), (0.14, 0.14, h), post)
    v = [(-w / 2 - 0.2, 0, z0 + h + 0.85), (w / 2 + 0.2, 0, z0 + h + 0.85), (w / 2 + 0.2, -depth - 0.3, z0 + h + 0.30), (-w / 2 - 0.2, -depth - 0.3, z0 + h + 0.30)]
    lo = [(p[0], p[1], p[2] - 0.10) for p in v]
    mb.poly(v + lo, [(0, 1, 2, 3), (4, 5, 6, 7), (2, 3, 7, 6), (0, 3, 7, 4), (1, 2, 6, 5)], roof)
    if rail:
        mb.box((0, -depth + 0.15, z0 + 1.2), (w, 0.08, 0.08), post)
        for k in range(int(w / 0.35)):
            mb.box((-w / 2 + 0.2 + k * 0.35, -depth + 0.15, z0 + 0.75), (0.05, 0.05, 0.9), post)


def sign_board(mb, c, w, h=0.7, mat="paint_white", trim="plank_dark", face=-1):
    c = Vector(c)
    mb.box(c, (w, 0.08, h), mat)
    mb.box(c + Vector((0, face * 0.03, 0)), (w - 0.2, 0.06, h * 0.22), trim)
    for dz in (-h / 2, h / 2):
        mb.box(c + Vector((0, 0, dz)), (w + 0.1, 0.12, 0.08), trim)


def building(mb, w=9.0, d=8.0, h=4.0, wall="plank", trim="paint_white", roof="shingle", stories=1, false_front=True, front_extra=1.6,
             porch_depth=2.2, windows=2, lit=False, bars=False, balcony=False, sign=True, double_door=False, seed=1):
    H = h * stories
    mb.box((0, d / 2, H / 2), (w, d, H), wall)
    gable(mb, (0, d / 2, H), (w, d), 1.6, roof, overhang=0.3, ridge_along="y", end_mat=wall)
    siding(mb, -w / 2, w / 2, 0, 0.1, H)
    if false_front:
        mb.box((0, -0.06, H + front_extra / 2 + 0.1), (w + 0.3, 0.20, front_extra + 0.2), wall)
        mb.box((0, -0.10, H + front_extra + 0.25), (w + 0.7, 0.34, 0.20), trim)
        mb.box((0, -0.10, H + front_extra + 0.45), (w * 0.4, 0.30, 0.28), trim)
        siding(mb, -w / 2, w / 2, -0.16, H, H + front_extra)
        if sign:
            sign_board(mb, (0, -0.22, H + front_extra * 0.5 + 0.1), w * 0.72, front_extra * 0.55, trim=wall if wall != "paint_white" else "plank_dark")
    for sx in (1, -1):
        mb.box((sx * (w / 2 - 0.02), -0.02, H / 2), (0.16, 0.16, H), trim)
    dw = 1.7 if double_door else 1.0
    door(mb, (0, 0, 0.30), dw, 2.2, trim=trim)
    for s in range(stories):
        z = s * h + 0.30 + 1.55
        for k in range(windows):
            x = (w / 2 - 1.3) * (1 if windows == 1 else (k / (windows - 1) * 2 - 1))
            if s == 0 and abs(x) < dw / 2 + 0.7:
                continue
            window(mb, (x, 0, z), trim=trim, lit=lit and (k + s) % 2 == 0, bars=bars)
        if s > 0:
            window(mb, (0, 0, z), trim=trim, lit=lit)
    for sx in (1, -1):
        for k in range(2):
            c = Vector((sx * w / 2, d * (0.3 + 0.4 * k), 1.85))
            mb.box(c + Vector((sx * 0.03, 0, 0)), (0.06, 0.9, 1.2), "glass")
            mb.box(c + Vector((sx * 0.05, 0, 0.62)), (0.10, 1.06, 0.09), trim)
            mb.box(c + Vector((sx * 0.05, 0, -0.62)), (0.10, 1.06, 0.09), trim)
    if porch_depth:
        porch(mb, w + 0.4, porch_depth, h - 0.75 if not balcony else h - 0.35, posts=4 if w < 10 else 5, post=trim, roof=roof if not balcony else "plank")
        if balcony:
            z = h + 0.0
            mb.box((0, -porch_depth / 2, z + 0.62), (w + 0.4, porch_depth, 0.12), "plank")
            mb.box((0, -porch_depth + 0.1, z + 1.55), (w + 0.4, 0.08, 0.08), trim)
            for k in range(int((w + 0.4) / 0.4)):
                mb.box((-(w + 0.4) / 2 + 0.2 + k * 0.4, -porch_depth + 0.1, z + 1.1), (0.05, 0.05, 0.9), trim)
    # chimney
    rng = random.Random(seed)
    cx = rng.choice((-1, 1)) * w * 0.28
    mb.box((cx, d * 0.7, H + 1.3), (0.6, 0.6, 1.8), "rock_dark")
    mb.box((cx, d * 0.7, H + 2.25), (0.75, 0.75, 0.14), "rock_grey")


def church(mb):
    building(mb, w=7.0, d=12.0, h=5.0, wall="paint_white", trim="plank_grey", roof="shingle", false_front=False, porch_depth=0, windows=2, sign=False)
    mb.box((0, 1.2, 8.2), (2.6, 2.6, 4.6), "paint_white")
    for a in range(4):
        e = Euler((0, 0, a * pi / 2)).to_matrix()
        mb.box(Vector((0, 1.2, 9.2)) + e @ Vector((0, -1.31, 0)), (1.2, 0.06, 1.5), "glass", rot=Euler((0, 0, a * pi / 2)))
    mb.loft([R((0, 1.2, 10.5), 1.55, p=4), R((0, 1.2, 14.2), 0.05, p=4)], "shingle", n=4, p=4)
    mb.box((0, 1.2, 14.9), (0.10, 0.10, 1.4), "plank_dark")
    mb.box((0, 1.2, 15.15), (0.7, 0.10, 0.10), "plank_dark")


def barn(mb):
    w, d, h = 10.0, 13.0, 4.6
    mb.box((0, d / 2, h / 2), (w, d, h), "paint_red")
    gable(mb, (0, d / 2, h), (w, d), 3.2, "shingle", overhang=0.4, ridge_along="y", end_mat="paint_red")
    siding(mb, -w / 2, w / 2, 0, 0.1, h, mat="iron_red", step=0.5)
    mb.box((0, -0.04, 1.75), (3.6, 0.10, 3.5), "plank_dark")
    for a in (-1, 1):
        mb.box((0, -0.10, 1.75), (0.16, 0.06, 4.9), "paint_white", rot=Euler((0, radians(46 * a), 0)))
    for p, s in (((0, -0.10, 3.56), (3.9, 0.08, 0.16)), ((0, -0.10, 0.06), (3.9, 0.08, 0.16)), ((-1.87, -0.10, 1.75), (0.16, 0.08, 3.5)), ((1.87, -0.10, 1.75), (0.16, 0.08, 3.5))):
        mb.box(p, s, "paint_white")
    mb.box((0, -0.05, 5.9), (1.3, 0.08, 1.3), "plank_dark")
    mb.box((0, -0.8, 7.2), (0.14, 1.8, 0.14), "plank_dark")


def outhouse(mb):
    mb.box((0, 0.6, 1.1), (1.2, 1.2, 2.2), "plank_grey")
    v = [(-0.75, -0.15, 2.5), (0.75, -0.15, 2.5), (0.75, 1.35, 2.15), (-0.75, 1.35, 2.15)]
    mb.poly(v + [(p[0], p[1], p[2] - 0.08) for p in v], [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)], "plank_dark")
    mb.box((0, -0.03, 1.0), (0.8, 0.06, 1.9), "plank")
    mb.box((0, -0.07, 1.65), (0.22, 0.04, 0.12), "glass")


def station(mb):
    """Depot with a long platform. The platform edge is at y = -7.5 (put that 2 m from the rail centre)."""
    w, d, h = 16.0, 6.5, 3.8
    mb.box((0, -2.5, 0.5), (30.0, 10.0, 1.0), "plank")                      # platform
    for k in range(60):
        mb.box((-14.75 + k * 0.5, -2.5, 1.002), (0.03, 10.0, 0.01), "plank_dark")
    mb.box((0, -7.45, 0.5), (30.0, 0.12, 1.02), "plank_dark")
    for sx in (1, -1):
        for k in range(4):
            mb.box((sx * 15.6, -5.0, 0.85 - k * 0.22), (1.2 + k * 0.0, 2.4, 0.12), "plank_dark", shift=(0, 0))
            mb.box((sx * (15.3 + k * 0.32), -5.0, 0.80 - k * 0.24), (0.34, 2.4, 0.12), "plank")
    z0 = 1.0
    mb.box((0, d / 2, z0 + h / 2), (w, d, h), "paint_yellow")
    mb.box((0, d / 2, z0 + 0.55), (w + 0.06, d + 0.06, 1.1), "paint_red")
    siding(mb, -w / 2, w / 2, 0, z0 + 1.1, z0 + h)
    gable(mb, (0, d / 2 - 1.0, z0 + h), (w + 3.0, d + 5.0), 2.4, "shingle", overhang=0.0, ridge_along="x", end_mat="paint_yellow")
    for k in range(5):
        x = -w / 2 - 1.0 + k * (w + 2.0) / 4
        mb.box((x, -3.3, z0 + h / 2), (0.18, 0.18, h), "paint_white")
        mb.box((x, -2.7, z0 + h - 0.45), (0.12, 1.4, 0.12), "paint_white", rot=Euler((radians(40), 0, 0)))
    mb.box((0, -0.35, z0 + 1.6), (3.0, 0.9, 3.0), "paint_yellow")            # ticket bay
    window(mb, (0, -0.8, z0 + 1.75), w=2.0, h=1.2, trim="paint_red", lit=True)
    for x in (-5.5, 5.5):
        door(mb, (x, 0, z0), 1.2, 2.3, trim="paint_red")
    for x in (-3.2, 3.2):
        window(mb, (x, 0, z0 + 1.75), trim="paint_red")
    sign_board(mb, (0, -3.35, z0 + h + 0.2), 5.0, 0.8, trim="plank_dark")
    for sx in (1, -1):
        sign_board(mb, (sx * 9.4, -0.2, z0 + h - 0.3), 0.06, 0.6)
    # clock
    mb.loft([R((0, -0.85, z0 + 3.05), 0.42), R((0, -0.95, z0 + 3.05), 0.42)], "paint_white", n=12, front=(0, 0, 1))
    mb.box((0, -0.97, z0 + 3.17), (0.04, 0.02, 0.26), "iron")
    mb.box((0.09, -0.97, z0 + 3.05), (0.20, 0.02, 0.04), "iron")
    # platform clutter
    bench(mb, (-7.5, -0.6, z0))
    bench(mb, (7.5, -0.6, z0))
    crate(mb, (11.5, -1.0, z0 + 0.45), 0.9)
    crate(mb, (12.5, -1.3, z0 + 0.4), 0.8, rotz=0.4, mat="plank_light")
    crate(mb, (11.9, -1.1, z0 + 1.25), 0.7, rotz=0.2)
    barrel(mb, (-11.5, -1.0, z0))
    barrel(mb, (-12.3, -1.5, z0))
    for x in (-13.0, 13.0):
        lamp_post(mb, (x, -6.6, z0))
    mb.box((0, d * 0.7, z0 + h + 2.3), (0.7, 0.7, 2.2), "rock_dark")


def bench(mb, c=(0, 0, 0)):
    c = Vector(c)
    mb.box(c + Vector((0, 0, 0.45)), (1.8, 0.5, 0.07), "plank")
    mb.box(c + Vector((0, 0.24, 0.85)), (1.8, 0.07, 0.5), "plank", rot=Euler((radians(-10), 0, 0)))
    for sx in (1, -1):
        mb.box(c + Vector((sx * 0.8, 0, 0.22)), (0.08, 0.5, 0.45), "iron")


def lamp_post(mb, c=(0, 0, 0), lit=True):
    c = Vector(c)
    cyl(mb, c, c + Vector((0, 0, 3.0)), 0.07, "iron", n=6, r2=0.05)
    mb.box(c + Vector((0, 0, 0.12)), (0.26, 0.26, 0.24), "iron")
    mb.loft([R(c + Vector((0, 0, 3.0)), 0.10, p=4), R(c + Vector((0, 0, 3.12)), 0.17, p=4), R(c + Vector((0, 0, 3.48)), 0.21, p=4)], "glass_lit" if lit else "glass", n=4, p=4)
    mb.loft([R(c + Vector((0, 0, 3.48)), 0.27, p=4), R(c + Vector((0, 0, 3.66)), 0.04, p=4)], "iron", n=4, p=4)


def lantern(mb):
    cyl(mb, (0, 0, 0), (0, 0, 0.05), 0.09, "iron", n=6)
    mb.loft([R((0, 0, 0.05), 0.06), R((0, 0, 0.14), 0.085), R((0, 0, 0.25), 0.06)], "glass_lit", n=6)
    mb.loft([R((0, 0, 0.25), 0.08), R((0, 0, 0.31), 0.03)], "iron", n=6)
    ring = [R((0.085 * cos(a), 0, 0.30 + 0.10 * sin(a)), 0.006) for a in [pi * i / 6 for i in range(7)]]
    mb.loft(ring, "iron", n=3)


def hitching_post(mb):
    for x in (-1.2, 1.2):
        mb.box((x, 0, 0.55), (0.16, 0.16, 1.2), "deadwood")
    cyl(mb, (-1.5, 0, 1.0), (1.5, 0, 1.0), 0.07, "plank_dark", n=6)


def trough(mb):
    mb.box((0, 0, 0.35), (2.2, 0.7, 0.5), "plank_dark", taper=(1.08, 1.2))
    mb.box((0, 0, 0.59), (2.2, 0.62, 0.03), "water")
    for x in (-0.8, 0.8):
        mb.box((x, 0, 0.06), (0.14, 0.8, 0.14), "plank_dark")


def hay_bale(mb):
    mb.box((0, 0, 0.3), (1.1, 0.6, 0.6), "mustard")
    for x in (-0.3, 0.3):
        mb.loft([R((x - 0.015, 0, 0.3), 0.31, 0.31, p=6), R((x + 0.015, 0, 0.3), 0.31, 0.31, p=6)], "rope", n=4, p=8, caps=(False, False), front=(0, 0, 1))


def sack(mb):
    mb.loft([R((0, 0, 0), 0.22, 0.30), R((0, 0, 0.30), 0.30, 0.38), R((0, 0, 0.55), 0.22, 0.26), R((0, 0, 0.62), 0.07), R((0, 0, 0.72), 0.12)], "canvas", n=7, p=2.5)


def bottle(mb, mat="bottle_green"):
    mb.loft([R((0, 0, 0), 0.036), R((0, 0, 0.16), 0.038), R((0, 0, 0.21), 0.016), R((0, 0, 0.29), 0.014), R((0, 0, 0.30), 0.018)], mat, n=8)


def bottle_crate(mb):
    """Tutorial target: four bottles on a crate, plus three tin cans."""
    crate(mb, (0, 0, 0.45), 0.9)
    for k, x in enumerate((-0.30, -0.10, 0.10, 0.30)):
        old = mb.xf
        from mathutils import Matrix
        mb.xf = Matrix.Translation((x, 0, 0.92))
        bottle(mb, ("bottle_green", "bottle_brown")[k % 2])
        mb.xf = old


def tin_can(mb):
    cyl(mb, (0, 0, 0), (0, 0, 0.11), 0.038, "tin", n=8)
    mb.loft([R((0, 0, 0.03), 0.0395), R((0, 0, 0.08), 0.0395)], "paint_red", n=8, caps=(False, False))


def wanted_board(mb):
    for x in (-0.7, 0.7):
        mb.box((x, 0, 1.1), (0.12, 0.12, 2.3), "deadwood")
    mb.box((0, 0, 1.6), (1.7, 0.08, 1.1), "plank_grey")
    for x, z, a in ((-0.4, 1.65, 0.05), (0.35, 1.7, -0.08), (0.0, 1.35, 0.12)):
        mb.box((x, -0.05, z), (0.42, 0.01, 0.56), "paper", rot=Euler((0, a, 0)))
        mb.box((x, -0.058, z + 0.03), (0.22, 0.01, 0.22), "plank_dark", rot=Euler((0, a, 0)))


def signpost(mb):
    mb.box((0, 0, 1.3), (0.14, 0.14, 2.7), "deadwood")
    for z, a, w in ((2.4, 0.2, 1.4), (2.0, -2.6, 1.2), (1.6, 1.4, 1.0)):
        e = Euler((0, 0, a))
        mb.box(Vector((0, 0, z)) + e.to_matrix() @ Vector((w / 2 - 0.1, -0.09, 0)), (w, 0.05, 0.28), "plank_light", rot=e)


def start_sign(mb):
    """Shootable menu target."""
    for x in (-1.1, 1.1):
        mb.box((x, 0, 1.2), (0.14, 0.14, 2.5), "deadwood")
    mb.box((0, 0, 2.0), (2.6, 0.10, 1.0), "paint_white")
    mb.box((0, -0.04, 2.0), (2.3, 0.06, 0.7), "paint_red")
    mb.loft([R((0, -0.08, 2.0), 0.26), R((0, -0.10, 2.0), 0.26)], "paint_white", n=12, front=(0, 0, 1))
    mb.loft([R((0, -0.10, 2.0), 0.12), R((0, -0.12, 2.0), 0.12)], "paint_red", n=12, front=(0, 0, 1))


def wagon(mb):
    mb.box((0, 0, 1.0), (1.5, 3.4, 0.12), "plank")
    for sx in (1, -1):
        mb.box((sx * 0.75, 0, 1.3), (0.08, 3.4, 0.6), "plank_dark", taper=(1.0, 1.0))
    for sy in (1, -1):
        mb.box((0, sy * 1.7, 1.3), (1.5, 0.08, 0.6), "plank_dark")
    for k in range(5):
        y = -1.5 + k * 0.75
        ring = [R((0.8 * cos(a), y, 1.6 + 1.1 * sin(a)), 0.03) for a in [pi * i / 8 for i in range(9)]]
        mb.loft(ring, "plank_dark", n=4)
    mb.loft([R((0, -1.6, 1.62), 0.84, 1.10, 0.0), R((0, 1.6, 1.62), 0.84, 1.10, 0.0)], "canvas", n=10, caps=(False, False), front=(0, 0, 1))
    from .scenery import wagon_wheel
    from mathutils import Matrix
    for sx in (1, -1):
        for y, r in ((-1.1, 0.55), (1.1, 0.7)):
            old = mb.xf
            mb.xf = Matrix.Translation((sx * 0.95, y, 0)) @ Euler((0, 0, pi / 2)).to_matrix().to_4x4()
            wagon_wheel(mb, r)
            mb.xf = old
    cyl(mb, (-0.3, -1.7, 0.9), (-0.3, -4.2, 0.6), 0.04, "plank_dark", n=5, front=(0, 0, 1))
    cyl(mb, (0.3, -1.7, 0.9), (0.3, -4.2, 0.6), 0.04, "plank_dark", n=5, front=(0, 0, 1))


# ---------------------------------------------------------------- tunnel and duck obstacles

CLEAR_H = 5.9      # under-side of anything the player must duck (rail top + car roof + a crouching cowboy)


def tunnel_portal(mb, seed=1):
    """Rock face with a timber-framed mouth. Origin on the track centre at ground level, mouth faces -Y."""
    rng = random.Random(seed)
    w, hgt = 5.6, CLEAR_H
    for sx in (1, -1):
        mb.box((sx * (w / 2 + 0.2), -0.3, hgt / 2), (0.5, 0.6, hgt), "plank_dark")
        mb.box((sx * (w / 2 - 0.9), -0.3, hgt - 0.7), (0.3, 0.4, 2.2), "plank_dark", rot=Euler((0, radians(-45 * sx), 0)))
    mb.box((0, -0.3, hgt + 0.3), (w + 1.6, 0.7, 0.6), "plank_dark")
    mb.box((0, -0.68, hgt + 0.3), (2.4, 0.08, 0.42), "paint_white")

    def j(a):
        return 1.0 + rng.uniform(-0.12, 0.12)
    for sx in (1, -1):
        mb.loft([R((sx * (w / 2 + 9.5), 6, -1), 10, 9, rfn=j, mat="rock_dark"), R((sx * (w / 2 + 8.5), 6, 9), 8.8, 8, rfn=j, mat="rock_red"),
                 R((sx * (w / 2 + 8.0), 7, 17), 7.4, 7, rfn=j, mat="rock_orange"), R((sx * (w / 2 + 7), 8, 23), 5, 5, rfn=j, mat="rock_pale"),
                 R((sx * (w / 2 + 7), 8, 25), 1.5, 1.5)], "rock_red", n=9, p=2.6)
    mb.loft([R((0, 7.5, hgt + 0.7), 9.5, 7.2, rfn=j, mat="rock_red"), R((0, 8, hgt + 8), 8.5, 7, rfn=j, mat="rock_orange"), R((0, 9, hgt + 15), 7, 6, rfn=j, mat="rock_pale"),
             R((0, 9, hgt + 19), 3, 3, rfn=j)], "rock_red", n=9, p=2.6)
    mb.box((0, 1.0, hgt / 2), (w, 1.6, hgt), "black")


def tunnel_segment(mb, length=20.0):
    """Inside of the tunnel, open at both ends. Timber ribs every 5 m with a lantern hook."""
    w, hgt = 5.6, CLEAR_H + 0.9
    mb.loft([R((0, 0, 0.0), w / 2 + 0.3, hgt, 0.0), R((0, length, 0.0), w / 2 + 0.3, hgt, 0.0)], "rock_dark", n=10, arc=(-92, 92), thick=-0.9,
            p=2.8, front=(0, 0, 1))
    y = 2.5
    while y < length:
        for sx in (1, -1):
            mb.box((sx * (w / 2 - 0.05), y, hgt * 0.42), (0.34, 0.34, hgt * 0.84), "plank_dark")
        mb.box((0, y, hgt * 0.86), (w + 0.2, 0.34, 0.34), "plank_dark")
        y += 5.0


def low_gantry(mb):
    """Signal gantry over the line. The beam is a duck obstacle."""
    for sx in (1, -1):
        mb.box((sx * 3.4, 0, (CLEAR_H + 1.2) / 2), (0.34, 0.34, CLEAR_H + 1.2), "plank_dark")
        mb.box((sx * 2.7, 0, CLEAR_H + 0.1), (0.2, 0.2, 2.0), "plank_dark", rot=Euler((0, radians(-45 * sx), 0)))
        mb.box((sx * 3.4, 0, 0.15), (0.9, 0.9, 0.3), "rock_grey")
    mb.box((0, 0, CLEAR_H + 0.25), (7.4, 0.4, 0.5), "plank_dark")
    mb.box((0, -0.22, CLEAR_H + 0.25), (3.0, 0.05, 0.4), "paint_yellow")
    for k in range(6):
        mb.box((-1.25 + k * 0.5, -0.25, CLEAR_H + 0.25), (0.22, 0.04, 0.5), "iron", rot=Euler((0, radians(35), 0)))
    lamp = Vector((0, 0, CLEAR_H + 0.5))
    mb.box(lamp + Vector((0, 0, 0.3)), (0.4, 0.3, 0.6), "iron")
    mb.loft([R(lamp + Vector((0, -0.16, 0.35)), 0.13), R(lamp + Vector((0, -0.20, 0.35)), 0.13)], "paint_red", n=8, front=(0, 0, 1))


# ---------------------------------------------------------------- rigged set pieces

def _rig(name, fn, bones, coll_name="WW_Town"):
    coll = get_collection(name, get_collection(coll_name))
    mb = MB(name + "_mesh")
    fn(mb)
    arm = build_armature(name, [("root", None, (0, 0, 0), (0, 0.5, 0))] + bones, coll)
    ob = mb.finish(coll, arm)
    arm["ww_kind"] = "setpiece"
    return arm, ob


def water_tower():
    """Track runs along Y at x = -5.5 from the tower. `spout_swing` brings the spout across the line at head height."""
    tz = CLEAR_H + 1.6
    W, S = {"root": 1}, {"spout": 1}

    def fn(mb):
        for sx in (1, -1):
            for sy in (1, -1):
                cyl(mb, (sx * 2.1, sy * 2.1, 0), (sx * 1.6, sy * 1.6, tz), 0.16, "deadwood", n=6, w=W)
        for lvl in (2.2, 5.0):
            k = 2.1 - 0.5 * lvl / tz
            for a in range(4):
                e = Euler((0, 0, a * pi / 2))
                mb.box(e.to_matrix() @ Vector((0, -k, lvl)), (k * 2, 0.12, 0.2), "plank_dark", w=W, rot=e)
                mb.box(e.to_matrix() @ Vector((0, -k - 0.02, lvl - 1.3)), (k * 2.3, 0.08, 0.16), "plank_dark", w=W, rot=Euler((0, radians(32), a * pi / 2)))
        mb.box((0, 0, tz + 0.1), (4.2, 4.2, 0.2), "plank_dark", w=W)
        mb.loft([R((0, 0, tz + 0.2), 2.05, w=W), R((0, 0, tz + 3.6), 2.2, w=W)], "plank", n=12)
        for z in (0.7, 1.9, 3.1):
            mb.loft([R((0, 0, tz + z - 0.06), 2.13 + z * 0.04, w=W), R((0, 0, tz + z + 0.06), 2.13 + z * 0.04, w=W)], "iron", n=12, caps=(False, False))
        mb.loft([R((0, 0, tz + 3.6), 2.5, w=W), R((0, 0, tz + 4.9), 0.12, w=W)], "shingle", n=12)
        cyl(mb, (0, 0, tz + 4.9), (0, 0, tz + 5.5), 0.04, "iron", n=4, w=W)
        ladder_x = 2.35
        for s in (-0.2, 0.2):
            mb.box((ladder_x, s, tz / 2 + 1.5), (0.05, 0.05, tz + 3.0), "plank_dark", w=W)
        z = 0.4
        while z < tz + 3.0:
            mb.box((ladder_x, 0, z), (0.04, 0.44, 0.04), "plank_dark", w=W)
            z += 0.4
        # spout: pivots under the tank edge nearest the track (-X), stowed pointing +Y
        piv = Vector((-2.0, 0, tz - 0.1))
        cyl(mb, piv + Vector((0, 0, 0.3)), piv + Vector((0, 0, -0.5)), 0.20, "iron", n=8, w=W)
        cyl(mb, piv + Vector((0, 0, -0.35)), piv + Vector((0, 4.6, -0.75)), 0.17, "tin", n=8, w=S, r2=0.13)
        cyl(mb, piv + Vector((0, 4.6, -0.75)), piv + Vector((0, 4.9, -1.35)), 0.13, "tin", n=8, w=S, r2=0.16)
        cyl(mb, piv + Vector((0, 0, 1.2)), piv + Vector((0, 3.2, -0.45)), 0.02, "rope", n=4, w=S)
        cyl(mb, piv + Vector((0, 4.4, -0.8)), piv + Vector((0, 4.4, -2.2)), 0.015, "rope", n=4, w=S)
    piv = (-2.0, 0, tz - 0.1)
    arm, ob = _rig("WaterTower", fn, [("spout", "root", piv, (piv[0], piv[1] + 1.0, piv[2]))])
    P = Poser(arm)
    make_action(arm, P, "WaterTower_spout_swing", [(0, {"spout": rot()}), (8, {"spout": rot(z=-8)}), (40, {"spout": rot(z=96)}), (48, {"spout": rot(z=90)}),
                                                   (110, {"spout": rot(z=90)}), (150, {"spout": rot(z=0)})])
    make_action(arm, P, "WaterTower_idle", [(0, {"spout": rot()}), (30, {"spout": rot(z=1.5)}), (60, {"spout": rot()})], loop=True)
    return arm, ob


def windmill():
    hub = Vector((0, -0.7, 9.2))
    W, F, T = {"root": 1}, {"fan": 1}, {"head": 1}

    def fn(mb):
        for sx in (1, -1):
            for sy in (1, -1):
                cyl(mb, (sx * 1.5, sy * 1.5, 0), (sx * 0.25, sy * 0.25, 8.8), 0.08, "plank_grey", n=4, w=W, p=4)
        for lvl in (1.5, 3.5, 5.5, 7.4):
            k = 1.5 - 1.25 * lvl / 8.8
            for a in range(4):
                e = Euler((0, 0, a * pi / 2))
                mb.box(e.to_matrix() @ Vector((0, -k, lvl)), (k * 2, 0.06, 0.10), "plank_grey", w=W, rot=e)
                mb.box(e.to_matrix() @ Vector((0, -k, lvl - 0.95)), (k * 2.6, 0.04, 0.08), "plank_grey", w=W, rot=Euler((0, radians(38), a * pi / 2)))
        mb.box((0, 0, 8.9), (0.7, 0.7, 0.2), "plank_dark", w=W)
        mb.box((0, 0.1, 9.2), (0.3, 1.4, 0.3), "iron", w=T)
        mb.poly([(0, 0.8, 9.2), (0, 2.6, 9.9), (0, 2.6, 8.5), (0, 1.2, 9.2)], [(0, 1, 3), (0, 3, 2)], "tin", w=T)
        mb.poly([(0.02, 0.8, 9.2), (0.02, 2.6, 9.9), (0.02, 2.6, 8.5), (0.02, 1.2, 9.2)], [(0, 3, 1), (0, 2, 3)], "tin", w=T)
        cyl(mb, hub + Vector((0, 0.1, 0)), hub + Vector((0, -0.12, 0)), 0.16, "iron", n=8, w=F, front=(0, 0, 1))
        n = 16
        for k in range(n):
            a = 2 * pi * k / n
            d = Vector((sin(a), 0, cos(a)))
            e = Euler((0, a, 0))
            mb.box(hub + d * 1.05, (0.34, 0.03, 1.5), "tin" if k % 2 else "paint_white", w=F,
                   rot=(e.to_matrix() @ Euler((0, 0, radians(24))).to_matrix()), taper=(1.5, 1.0))
        for rr in (0.45, 1.75):
            mb.loft([R(hub + Vector((0, -0.02, 0)), rr), R(hub + Vector((0, 0.02, 0)), rr), R(hub + Vector((0, 0.02, 0)), rr - 0.05), R(hub + Vector((0, -0.02, 0)), rr - 0.05)],
                    "iron", n=16, caps=(False, False), front=(0, 0, 1))
    arm, ob = _rig("Windmill", fn, [("head", "root", (0, 0, 9.0), (0, 0, 9.6)), ("fan", "head", tuple(hub), (hub.x, hub.y - 0.5, hub.z))])
    P = Poser(arm)
    make_action(arm, P, "Windmill_spin", [(f, {"fan": rot(y=(360 * f / 48) % 360)}) for f in range(0, 49, 2)], loop=True)
    return arm, ob


def station_bell():
    """Shoot it to depart."""
    piv = Vector((0, 0, 2.75))
    W, B, C = {"root": 1}, {"bell": 1}, {"clapper": 1}

    def fn(mb):
        for sx in (1, -1):
            mb.box((sx * 0.55, 0, 1.5), (0.16, 0.16, 3.0), "plank_dark", w=W)
            mb.box((sx * 0.55, 0, 0.1), (0.5, 0.7, 0.2), "rock_grey", w=W)
        mb.box((0, 0, 3.05), (1.5, 0.22, 0.18), "plank_dark", w=W)
        cyl(mb, piv + Vector((-0.5, 0, 0)), piv + Vector((0.5, 0, 0)), 0.035, "iron", n=6, w=B, front=(0, 0, 1))
        mb.loft([R(piv + Vector((0, 0, -0.02)), 0.07, w=B), R(piv + Vector((0, 0, -0.12)), 0.17, w=B), R(piv + Vector((0, 0, -0.45)), 0.25, w=B),
                 R(piv + Vector((0, 0, -0.62)), 0.36, w=B), R(piv + Vector((0, 0, -0.66)), 0.38, w=B)], "brass", n=10)
        cyl(mb, piv + Vector((0, 0, -0.15)), piv + Vector((0, 0, -0.70)), 0.015, "iron", n=4, w=C)
        mb.loft([R(piv + Vector((0, 0, -0.66)), 0.03, w=C), R(piv + Vector((0, 0, -0.72)), 0.06, w=C), R(piv + Vector((0, 0, -0.80)), 0.03, w=C)], "iron", n=6)
        cyl(mb, piv + Vector((0.42, 0, 0)), piv + Vector((0.42, 0.1, -1.5)), 0.012, "rope", n=4, w=B)
    arm, ob = _rig("StationBell", fn, [("bell", "root", tuple(piv), (piv.x, piv.y, piv.z - 0.5)), ("clapper", "bell", (piv.x, piv.y, piv.z - 0.15), (piv.x, piv.y, piv.z - 0.7))])
    P = Poser(arm)
    keys = []
    for f in range(0, 61, 3):
        t = f / 60
        amp = 34 * (1 - t) ** 1.5
        keys.append((f, {"bell": rot(x=amp * cos(2 * pi * t * 4)), "clapper": rot(x=-amp * 1.3 * cos(2 * pi * t * 4 - 0.9))}))
    make_action(arm, P, "StationBell_ring", keys)
    return arm, ob


def saloon():
    D = 0.62

    def fn(mb):
        mb.default_w = {"root": 1}
        building(mb, w=11.0, d=10.0, h=3.8, wall="plank_light", trim="paint_red", roof="shingle", stories=2, front_extra=1.4, windows=3, lit=True,
                 balcony=True, double_door=True, seed=2)
        mb.box((0, -0.02, 0.30 + 1.1), (1.75, 0.12, 2.2), "black")          # dark doorway behind the batwings
        for sd, sx in (("l", 1), ("r", -1)):
            w = {"door_" + sd: 1}
            mb.box((sx * (0.85 - D / 2 - 0.02), -0.16, 0.30 + 1.15), (D, 0.05, 0.95), "plank", w=w)
            for k in range(3):
                mb.box((sx * (0.85 - D / 2 - 0.02), -0.19, 0.30 + 0.85 + k * 0.3), (D * 0.8, 0.02, 0.05), "plank_dark", w=w)
        mb.default_w = None
    bones = [("door_l", "root", (0.85, -0.16, 1.45), (0.85, -0.16, 1.95)), ("door_r", "root", (-0.85, -0.16, 1.45), (-0.85, -0.16, 1.95))]
    arm, ob = _rig("Saloon", fn, bones)
    P = Poser(arm)
    keys = []
    for f in range(0, 49, 2):
        t = f / 48
        a = 70 * (1 - t) ** 2 * cos(2 * pi * t * 3)
        keys.append((f, {"door_l": rot(z=-a), "door_r": rot(z=a * 0.9)}))
    make_action(arm, P, "Saloon_doors_swing", keys)
    return arm, ob


BUILDINGS = {
    "Station": (station, {}),
    "GeneralStore": (building, dict(w=9.0, d=9.0, h=4.0, wall="paint_green", trim="paint_white", seed=1)),
    "Sheriff": (building, dict(w=7.0, d=7.0, h=3.6, wall="plank_grey", trim="plank_dark", windows=2, bars=True, seed=3)),
    "Bank": (building, dict(w=8.0, d=8.0, h=4.4, wall="paint_white", trim="paint_blue", front_extra=2.0, seed=4)),
    "Hotel": (building, dict(w=10.0, d=9.0, h=3.6, wall="paint_blue", trim="paint_white", stories=2, windows=3, lit=True, balcony=True, seed=5)),
    "Shack": (building, dict(w=5.0, d=5.0, h=3.0, wall="plank_grey", trim="plank_dark", false_front=False, windows=1, porch_depth=1.6, sign=False, seed=6)),
    "Church": (church, {}), "Barn": (barn, {}), "Outhouse": (outhouse, {}),
}
PROPS = {
    "Crate": (lambda mb: crate(mb, (0, 0, 0.45), 0.9), {}), "Crate_Small": (lambda mb: crate(mb, (0, 0, 0.3), 0.6, mat="plank_light"), {}),
    "Barrel": (lambda mb: barrel(mb, (0, 0, 0)), {}), "Bottle_Green": (bottle, {}), "Bottle_Brown": (bottle, dict(mat="bottle_brown")),
    "BottleCrate": (bottle_crate, {}), "TinCan": (tin_can, {}), "Bench": (bench, {}), "LampPost": (lamp_post, {}), "Lantern": (lantern, {}),
    "HitchingPost": (hitching_post, {}), "Trough": (trough, {}), "HayBale": (hay_bale, {}), "Sack": (sack, {}), "WantedBoard": (wanted_board, {}),
    "Signpost": (signpost, {}), "StartSign": (start_sign, {}), "Wagon": (wagon, {}),
    "TunnelPortal": (tunnel_portal, {}), "TunnelSegment_20m": (tunnel_segment, {}), "LowGantry": (low_gantry, {}),
}
RIGGED = {"WaterTower": water_tower, "Windmill": windmill, "StationBell": station_bell, "Saloon": saloon}
