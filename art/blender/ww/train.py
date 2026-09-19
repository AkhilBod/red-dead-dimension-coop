"""Steam train. Forward is -Y, origin on the rail top at the middle of each vehicle. All wheels are rigged.

Every rig has one looping action `<name>_roll`: 24 frames = one wheel turn. Scale the play rate by speed / wheel radius.
"""
import random
from math import cos, pi, radians, sin, sqrt

from mathutils import Euler, Vector

from .core import MB, R, Poser, aim, build_armature, cyl, gable, get_collection, make_action, rot

GAUGE = 0.78            # half distance between rail centres
CAR_W = 2.9
FLOOR = 1.05
LIVERY = {
    "player": dict(body="paint_green", trim="paint_yellow", roof="shingle", frame="iron", accent="paint_red"),
    "bandit": dict(body="paint_red", trim="iron", roof="iron", frame="iron", accent="paint_yellow"),
}


# ---------------------------------------------------------------- parts

def wheel(mb, y, r, w, x_sign, spokes=0, tyre="steel_dark", web="iron_red"):
    x0 = GAUGE * x_sign
    o = 0.07 * x_sign
    # tread + flange (flange on the inside)
    mb.loft([R((x0 - o * 1.2, y, r), r + 0.055, w=w), R((x0 - o * 0.7, y, r), r + 0.055, w=w), R((x0 - o * 0.7, y, r), r, w=w),
             R((x0 + o, y, r), r * 0.985, w=w)], tyre, n=14, front=(0, 0, 1))
    if spokes:
        mb.loft([R((x0 + o * 0.9, y, r), r * 0.80, w=w), R((x0 + o * 1.05, y, r), r * 0.80, w=w)], "iron", n=14, front=(0, 0, 1))
        cyl(mb, (x0 - o, y, r), (x0 + o * 1.9, y, r), r * 0.20, web, n=8, w=w, front=(0, 0, 1))
        for k in range(spokes):
            a = 2 * pi * k / spokes
            c = Vector((x0 + o * 1.25, y + sin(a) * r * 0.52, r + cos(a) * r * 0.52))
            mb.box(c, (0.035, 0.07, r * 0.72), web, w=w, rot=Euler((-a, 0, 0)))
        mb.loft([R((x0 + o * 0.8, y, r), r * 0.93, w=w), R((x0 + o * 1.6, y, r), r * 0.93, w=w), R((x0 + o * 1.6, y, r), r * 0.78, w=w)],
                web, n=14, front=(0, 0, 1), caps=(False, False))
    else:
        mb.loft([R((x0 + o, y, r), r * 0.985, w=w), R((x0 + o * 1.5, y, r), r * 0.55, w=w), R((x0 + o * 2.0, y, r), r * 0.22, w=w)],
                web, n=14, front=(0, 0, 1))


def axle(mb, y, r, w, spokes=0):
    cyl(mb, (-GAUGE, y, r), (GAUGE, y, r), 0.055, "iron", n=6, w=w, front=(0, 0, 1))
    for sx in (1, -1):
        wheel(mb, y, r, w, sx, spokes)


def bogie(mb, y, idx, r=0.42, base=1.7):
    """Two-axle truck. Returns bone specs."""
    bones = []
    W = {"root": 1}
    for k, dy in enumerate((-base / 2, base / 2)):
        bn = f"axle_{idx + k}"
        axle(mb, y + dy, r, {bn: 1})
        bones.append((bn, "root", (0, y + dy, r), (0.3, y + dy, r)))
    for sx in (1, -1):
        mb.box((sx * (GAUGE + 0.17), y, r + 0.02), (0.08, base + 0.7, 0.16), "iron", w=W)
        mb.box((sx * (GAUGE + 0.17), y, r + 0.17), (0.10, 0.75, 0.16), "iron", w=W)
        for dy in (-base / 2, base / 2):
            mb.box((sx * (GAUGE + 0.19), y + dy, r), (0.10, 0.26, 0.26), "steel_dark", w=W)
    mb.box((0, y, r + 0.30), (GAUGE * 2 + 0.5, 0.40, 0.18), "iron", w=W)
    return bones


def coupler(mb, y, sgn):
    W = {"root": 1}
    mb.box((0, y + sgn * 0.22, 0.72), (0.20, 0.50, 0.16), "iron", w=W)
    mb.box((0, y + sgn * 0.50, 0.72), (0.30, 0.16, 0.24), "steel_dark", w=W)


def underframe(mb, L, lv):
    W = {"root": 1}
    mb.box((0, 0, FLOOR - 0.11), (CAR_W, L, 0.22), lv["frame"], w=W)
    for sx in (1, -1):
        mb.box((sx * 0.55, 0, FLOOR - 0.32), (0.16, L * 0.62, 0.24), lv["frame"], w=W)
    for sg in (1, -1):
        coupler(mb, sg * L / 2, sg)
    bones = bogie(mb, -L / 2 + 2.2, 1) + bogie(mb, L / 2 - 2.2, 3)
    return bones


def ladder(mb, x, y, z0, z1, face="x"):
    W = {"root": 1}
    d = (0.0, 0.22) if face == "x" else (0.22, 0.0)
    for s in (-1, 1):
        mb.box((x + d[0] * s, y + d[1] * s, (z0 + z1) / 2), (0.05, 0.05, z1 - z0), "iron", w=W)
    z = z0 + 0.25
    while z < z1:
        mb.box((x, y, z), (0.05 if face == "x" else 0.44, 0.44 if face == "x" else 0.05, 0.04), "iron", w=W)
        z += 0.36


def arched_roof(mb, L, z, lv, rise=0.42, over=0.16, walkway=True):
    W = {"root": 1}
    mb.loft([R((0, -L / 2 - over, z), CAR_W / 2 + over, rise, 0.06, w=W), R((0, L / 2 + over, z), CAR_W / 2 + over, rise, 0.06, w=W)],
            lv["roof"], n=12, p=2.2, front=(0, 0, 1))
    if walkway:
        mb.box((0, 0, z + rise + 0.035), (0.70, L + 0.1, 0.07), "plank", w=W)
        for k in range(int(L / 1.5) + 1):
            mb.box((0, -L / 2 + k * 1.5, z + rise + 0.01), (0.86, 0.10, 0.06), "plank_dark", w=W)
    return z + rise + 0.07


def end_platform(mb, L, sgn, lv, roofed=True):
    W = {"root": 1}
    y = sgn * (L / 2 + 0.45)
    mb.box((0, y, FLOOR - 0.06), (CAR_W - 0.2, 0.9, 0.12), "plank_dark", w=W)
    for sx in (1, -1):
        mb.box((sx * (CAR_W / 2 - 0.18), y + sgn * 0.38, FLOOR + 0.5), (0.06, 0.06, 1.0), lv["frame"], w=W)
        mb.box((sx * (CAR_W / 2 - 0.55), y + sgn * 0.38, FLOOR + 0.5), (0.06, 0.06, 1.0), lv["frame"], w=W)
        mb.box((sx * (CAR_W / 2 - 0.36), y + sgn * 0.38, FLOOR + 1.0), (0.46, 0.06, 0.06), lv["trim"], w=W)
        mb.box((sx * 1.0, y + sgn * 0.62, FLOOR - 0.45), (0.6, 0.30, 0.06), "plank_dark", w=W)
        mb.box((sx * 1.0, y + sgn * 0.78, FLOOR - 0.78), (0.6, 0.30, 0.06), "plank_dark", w=W)


# ---------------------------------------------------------------- cars

def passenger_car(mb, lv, L=13.0, windows=8):
    W = {"root": 1}
    bones = underframe(mb, L + 1.8, lv)
    z0, zs, zt, z1 = FLOOR, FLOOR + 0.95, FLOOR + 1.95, FLOOR + 2.35
    mb.box((0, 0, z0 + 0.03), (CAR_W - 0.1, L, 0.06), "plank_dark", w=W)
    pitch = L / windows
    ww = pitch * 0.62
    for sx in (1, -1):
        x = sx * (CAR_W / 2 - 0.05)
        mb.box((x, 0, (z0 + zs) / 2), (0.10, L, zs - z0), lv["body"], w=W)
        mb.box((x, 0, (zt + z1) / 2), (0.10, L, z1 - zt), lv["body"], w=W)
        mb.box((x + sx * 0.03, 0, zs + 0.02), (0.14, L, 0.08), lv["trim"], w=W)
        mb.box((x + sx * 0.03, 0, z0 + 0.10), (0.14, L, 0.10), lv["trim"], w=W)
        for k in range(windows + 1):
            y = -L / 2 + k * pitch
            wd = (pitch - ww) if 0 < k < windows else (pitch - ww) / 2
            yy = y if 0 < k < windows else y + (wd / 2 if k == 0 else -wd / 2)
            mb.box((x, yy, (zs + zt) / 2), (0.10, wd, zt - zs), lv["body"], w=W)
        for k in range(windows):
            y = -L / 2 + (k + 0.5) * pitch
            mb.box((x + sx * 0.02, y, zt - 0.04), (0.13, ww + 0.1, 0.08), lv["trim"], w=W)
            mb.box((x - sx * 0.03, y, zt - 0.22), (0.02, ww, 0.30), "canvas", w=W)   # half-drawn blind
    for sg in (1, -1):   # end walls with a door
        y = sg * (L / 2 - 0.05)
        for sx in (1, -1):
            mb.box((sx * (CAR_W / 4 + 0.22), y, (z0 + z1) / 2), (CAR_W / 2 - 0.44, 0.10, z1 - z0), lv["body"], w=W)
        mb.box((0, y, z1 - 0.18), (0.9, 0.10, 0.36), lv["body"], w=W)
        mb.box((0, y + sg * 0.02, z0 + 1.0), (0.84, 0.06, 1.98), "plank_dark", w=W)
        mb.box((0, y + sg * 0.05, z0 + 1.45), (0.5, 0.04, 0.6), "glass", w=W)
        end_platform(mb, L, sg, lv)
    for k in range(windows):   # seats
        y = -L / 2 + (k + 0.5) * pitch
        for sx in (1, -1):
            mb.box((sx * 0.85, y, z0 + 0.30), (0.9, 0.5, 0.5), "paint_red", w=W)
            mb.box((sx * 0.85, y + 0.28, z0 + 0.70), (0.9, 0.10, 0.9), "paint_red", w=W)
    top = arched_roof(mb, L + 1.8, z1, lv, walkway=False)
    mb.box((0, 0, top + 0.10), (1.25, L * 0.86, 0.34), lv["body"], w=W)   # clerestory
    for k in range(7):
        y = -L * 0.36 + k * L * 0.12
        for sx in (1, -1):
            mb.box((sx * 0.635, y, top + 0.12), (0.02, 0.55, 0.16), "glass_lit", w=W)
    mb.loft([R((0, -L * 0.45, top + 0.27), 0.78, 0.16, 0.02, w=W), R((0, L * 0.45, top + 0.27), 0.78, 0.16, 0.02, w=W)], lv["roof"], n=10,
            p=2.2, front=(0, 0, 1))
    mb.box((0, 0, top + 0.46), (0.62, L * 0.9, 0.06), "plank", w=W)
    return bones


def boxcar(mb, lv, L=11.0):
    W = {"root": 1}
    bones = underframe(mb, L, lv)
    z0, z1 = FLOOR, FLOOR + 2.45
    dw = 2.2
    for sx, dn in ((1, "door_l"), (-1, "door_r")):
        x = sx * (CAR_W / 2 - 0.05)
        for sg in (1, -1):
            seg = (L - dw) / 2
            mb.box((x, sg * (dw / 2 + seg / 2), (z0 + z1) / 2), (0.10, seg, z1 - z0), lv["body"], w=W)
            n = int(seg / 0.45)
            for k in range(1, n):
                mb.box((x + sx * 0.055, sg * (dw / 2 + k * seg / n), (z0 + z1) / 2), (0.03, 0.05, z1 - z0 - 0.1), "plank_dark", w=W)
        mb.box((x, 0, z1 - 0.12), (0.10, dw, 0.24), lv["body"], w=W)
        mb.box((x + sx * 0.10, 0, z1 - 0.05), (0.06, L * 0.8, 0.07), "iron", w=W)     # door rail
        D = {dn: 1}
        mb.box((x + sx * 0.09, 0, z0 + 1.12), (0.08, dw + 0.12, 2.22), "plank", w=D)
        for a in (-1, 1):
            mb.box((x + sx * 0.14, 0, z0 + 1.12), (0.04, dw * 1.30, 0.10), "plank_dark", w=D, rot=Euler((radians(43 * a), 0, 0)))
        mb.box((x + sx * 0.15, -dw / 2 + 0.2, z0 + 1.1), (0.06, 0.06, 0.30), "iron", w=D)
        bones.append((dn, "root", (x, 0, z0 + 1.1), (x, 0.4, z0 + 1.1)))
    for sg in (1, -1):
        mb.box((0, sg * (L / 2 - 0.05), (z0 + z1) / 2), (CAR_W, 0.10, z1 - z0), lv["body"], w=W)
        ladder(mb, CAR_W / 2 - 0.45, sg * (L / 2 + 0.04), 0.5, z1 + 0.35, face="y")
    mb.box((0, 0, z0 + 0.03), (CAR_W - 0.1, L, 0.06), "plank_dark", w=W)
    mb.box((0, 0, z1), (CAR_W + 0.06, L + 0.06, 0.10), lv["trim"], w=W)
    arched_roof(mb, L, z1 + 0.05, lv, rise=0.34)
    cyl(mb, (0.9, L / 2 + 0.12, z1 + 0.3), (0.9, L / 2 + 0.12, z1 + 0.85), 0.025, "iron", w=W)
    cyl(mb, (0.9, L / 2 + 0.12, z1 + 0.83), (0.9, L / 2 + 0.12, z1 + 0.88), 0.22, "iron", n=10, w=W)
    return bones


def crate(mb, c, s=0.9, w=None, mat="plank", rotz=0.0):
    e = Euler((0, 0, rotz))
    c = Vector(c)
    mb.box(c, (s, s, s), mat, w=w, rot=e)
    t = s * 0.09
    for sx in (-1, 1):
        for sy in (-1, 1):
            mb.box(c + e.to_matrix() @ Vector((sx * (s / 2 - t / 2), sy * (s / 2 + 0.01), 0)), (t, 0.03, s), "plank_dark", w=w, rot=e)
            mb.box(c + e.to_matrix() @ Vector((sx * (s / 2 + 0.01), sy * (s / 2 - t / 2), 0)), (0.03, t, s), "plank_dark", w=w, rot=e)
    for sz in (-1, 1):
        mb.box(c + Vector((0, 0, sz * (s / 2 - t / 2))), (s + 0.05, s + 0.05, t), "plank_dark", w=w, rot=e)


def barrel(mb, c, h=0.95, r=0.34, w=None, mat="plank"):
    c = Vector(c)
    mb.loft([R(c, r * 0.84, w=w), R(c + Vector((0, 0, h * 0.30)), r, w=w), R(c + Vector((0, 0, h * 0.70)), r, w=w),
             R(c + Vector((0, 0, h)), r * 0.84, w=w)], mat, n=10)
    for t in (0.14, 0.86):
        rr = r * 0.93
        mb.loft([R(c + Vector((0, 0, h * t - 0.03)), rr + 0.012, w=w), R(c + Vector((0, 0, h * t + 0.03)), rr + 0.012, w=w)], "iron", n=10,
                caps=(False, False))


def flatcar(mb, lv, L=11.0, cargo="crates", seed=3):
    W = {"root": 1}
    bones = underframe(mb, L, lv)
    mb.box((0, 0, FLOOR + 0.05), (CAR_W + 0.05, L, 0.10), "plank", w=W)
    for k in range(int(L / 0.5)):
        mb.box((0, -L / 2 + 0.25 + k * 0.5, FLOOR + 0.102), (CAR_W, 0.03, 0.01), "plank_dark", w=W)
    for sx in (1, -1):
        for k in range(6):
            mb.box((sx * (CAR_W / 2 + 0.04), -L / 2 + 0.9 + k * (L - 1.8) / 5, FLOOR + 0.05), (0.10, 0.16, 0.26), "iron", w=W)
    z = FLOOR + 0.10
    rng = random.Random(seed)
    if cargo == "crates":
        for y, x, s in ((-3.6, -0.6, 1.0), (-3.5, 0.55, 0.9), (-2.4, -0.2, 1.1), (0.4, 0.7, 1.0), (1.5, -0.6, 1.2), (3.4, 0.2, 1.0), (3.5, -0.85, 0.8)):
            crate(mb, (x, y, z + s / 2), s, w=W, rotz=rng.uniform(-0.2, 0.2), mat=rng.choice(("plank", "plank_light")))
        crate(mb, (-0.35, -3.55, z + 1.0 + 0.4), 0.8, w=W, rotz=0.3)
        crate(mb, (1.5 * 0 - 0.5, 1.5, z + 1.2 + 0.4), 0.8, w=W, rotz=-0.2, mat="plank_light")
        for y, x in ((-1.0, 0.8), (-0.9, 0.05), (2.5, 0.9)):
            barrel(mb, (x, y, z), w=W)
    elif cargo == "gatling":
        for k in range(14):   # sandbag horseshoe, wall toward -X (the main line)
            a = radians(-105 + k * 210 / 13)
            for lvl in range(3):
                rr = 1.25
                p = Vector((-0.2 - rr * cos(a), rr * sin(a) * 1.25, z + 0.13 + lvl * 0.24))
                mb.loft([R(p + Vector((-sin(a), cos(a) * 1.25, 0)).normalized() * -0.26, 0.10, 0.13, w=W),
                         R(p, 0.17, 0.14, w=W), R(p + Vector((-sin(a), cos(a) * 1.25, 0)).normalized() * 0.26, 0.10, 0.13, w=W)],
                        "canvas", n=6, p=2.6, front=(0, 0, 1))
        crate(mb, (-0.8, 3.6, z + 0.45), 0.9, w=W)
        crate(mb, (0.3, 3.9, z + 0.4), 0.8, w=W, rotz=0.3)
        barrel(mb, (-0.7, -3.8, z), w=W)
    elif cargo == "logs":
        for i, (x, zz) in enumerate(((-0.8, 0.35), (0, 0.35), (0.8, 0.35), (-0.4, 0.98), (0.4, 0.98), (0, 1.6))):
            cyl(mb, (x, -L / 2 + 0.6 + rng.uniform(0, 0.3), z + zz), (x, L / 2 - 0.6 - rng.uniform(0, 0.3), z + zz), 0.36, "deadwood", n=8, w=W,
                front=(0, 0, 1))
        for sx in (1, -1):
            for y in (-3, 0, 3):
                mb.box((sx * 1.3, y, z + 0.9), (0.10, 0.12, 1.8), "plank_dark", w=W)
    return bones


def caboose(mb, lv, L=8.0):
    W = {"root": 1}
    bones = underframe(mb, L + 1.8, lv)
    z0, z1 = FLOOR, FLOOR + 2.3
    body = lv["accent"]
    for sx in (1, -1):
        x = sx * (CAR_W / 2 - 0.05)
        mb.box((x, 0, (z0 + z1) / 2), (0.10, L, z1 - z0), body, w=W)
        for y in (-2.2, 2.2):
            mb.box((x + sx * 0.04, y, z0 + 1.45), (0.06, 0.8, 0.7), "glass", w=W)
            mb.box((x + sx * 0.05, y, z0 + 1.45), (0.04, 0.92, 0.06), lv["trim"], w=W)
        mb.box((x + sx * 0.03, 0, z0 + 0.12), (0.14, L, 0.12), lv["trim"], w=W)
    for sg in (1, -1):
        mb.box((0, sg * (L / 2 - 0.05), (z0 + z1) / 2), (CAR_W, 0.10, z1 - z0), body, w=W)
        mb.box((0, sg * (L / 2), z0 + 1.0), (0.84, 0.06, 1.98), "plank_dark", w=W)
        end_platform(mb, L, sg, lv)
    top = arched_roof(mb, L + 1.8, z1, lv, rise=0.36)
    mb.box((0, 0.3, top + 0.30), (1.7, 2.0, 0.85), body, w=W)   # cupola
    for sx in (1, -1):
        mb.box((sx * 0.86, 0.3, top + 0.42), (0.04, 1.5, 0.40), "glass_lit", w=W)
    for sg in (1, -1):
        mb.box((0, 0.3 + sg * 1.01, top + 0.42), (1.2, 0.04, 0.40), "glass_lit", w=W)
    mb.loft([R((0, -0.85, top + 0.72), 1.0, 0.22, 0.02, w=W), R((0, 1.45, top + 0.72), 1.0, 0.22, 0.02, w=W)], lv["roof"], n=10, p=2.2,
            front=(0, 0, 1))
    cyl(mb, (-0.8, -2.5, z1 + 0.3), (-0.8, -2.5, z1 + 1.2), 0.10, "iron", w=W)
    cyl(mb, (-0.8, -2.5, z1 + 1.2), (-0.8, -2.5, z1 + 1.3), 0.16, "iron", w=W)
    return bones


def tender(mb, lv, L=6.4):
    W = {"root": 1}
    bones = underframe(mb, L, lv)
    z0, z1 = FLOOR, FLOOR + 1.55
    for sx in (1, -1):
        mb.box((sx * (CAR_W / 2 - 0.10), 0, (z0 + z1) / 2), (0.12, L, z1 - z0), "iron", w=W)
        mb.box((sx * (CAR_W / 2 - 0.03), 0, z1 - 0.06), (0.10, L, 0.10), lv["trim"], w=W)
        mb.box((sx * (CAR_W / 2 - 0.03), 0, z0 + 0.5), (0.04, L * 0.8, 0.06), lv["trim"], w=W)
    mb.box((0, L / 2 - 0.06, (z0 + z1) / 2), (CAR_W - 0.1, 0.12, z1 - z0), "iron", w=W)
    mb.box((0, -L / 2 + 0.06, z0 + 0.5), (CAR_W - 0.1, 0.12, 1.0), "iron", w=W)
    mb.box((0, L / 2 - 1.2, z1 - 0.1), (CAR_W - 0.2, 2.2, 0.12), "iron", w=W)       # water tank deck
    cyl(mb, (0, L / 2 - 1.0, z1 - 0.05), (0, L / 2 - 1.0, z1 + 0.12), 0.30, lv["trim"], n=10, w=W)
    rng = random.Random(7)

    def jit(a):
        return 0.82 + 0.3 * rng.random()
    mb.loft([R((0, -0.9, z0 + 0.9), 1.25, 2.1, w=W, rfn=jit), R((0, -0.9, z1 + 0.05), 1.15, 1.9, w=W, rfn=jit),
             R((0, -1.0, z1 + 0.45), 0.7, 1.2, w=W, rfn=jit), R((0, -1.0, z1 + 0.62), 0.2, 0.4, w=W)], "coal", n=9, p=2.6)
    return bones


# ---------------------------------------------------------------- locomotive

DRV_R = 0.86
DRV_Y = (1.35, 3.15)
CRANK = 0.34
CROSS_Y, CROSS_Z = -2.35, DRV_R
ROD_L = abs(CROSS_Y - DRV_Y[0]) + 0.35


def locomotive(mb, lv):
    W = {"root": 1}
    bones = []
    bz = 2.30        # boiler axis height
    br = 0.78
    # frame + running boards
    mb.box((0, 0.6, 1.18), (1.5, 8.0, 0.24), "iron", w=W)
    for sx in (1, -1):
        mb.box((sx * 1.18, 0.4, 1.62), (0.55, 5.6, 0.07), lv["trim"], w=W)
        mb.box((sx * 1.42, 0.4, 1.55), (0.06, 5.6, 0.18), lv["accent"], w=W)
    # boiler, smokebox, firebox
    mb.loft([R((0, -3.25, bz), br * 0.96, w=W), R((0, -3.15, bz), br * 1.02, w=W), R((0, -2.2, bz), br * 1.02, w=W), R((0, -2.2, bz), br, w=W),
             R((0, 2.2, bz), br, w=W)], "iron", n=14, front=(0, 0, 1))
    mb.loft([R((0, -3.26, bz), br * 0.72, w=W), R((0, -3.34, bz), br * 0.66, w=W)], "steel_dark", n=14, front=(0, 0, 1))
    cyl(mb, (0, -3.34, bz), (0, -3.40, bz), 0.10, lv["trim"], w=W, front=(0, 0, 1))
    for y in (-1.5, -0.4, 0.7, 1.8):
        mb.loft([R((0, y - 0.05, bz), br + 0.025, w=W), R((0, y + 0.05, bz), br + 0.025, w=W)], lv["trim"], n=14, front=(0, 0, 1),
                caps=(False, False))
    mb.box((0, 2.0, 1.95), (1.9, 1.6, 1.5), "iron", w=W)
    # stack (diamond), domes, bell, whistle, headlamp
    cyl(mb, (0, -2.7, bz + br - 0.1), (0, -2.7, bz + br + 0.7), 0.20, "iron", n=10, w=W)
    mb.loft([R((0, -2.7, bz + br + 0.7), 0.22, w=W), R((0, -2.7, bz + br + 1.20), 0.56, w=W), R((0, -2.7, bz + br + 1.42), 0.46, w=W),
             R((0, -2.7, bz + br + 1.50), 0.30, w=W)], "iron", n=10)
    mb.loft([R((0, -2.7, bz + br + 1.16), 0.585, w=W), R((0, -2.7, bz + br + 1.24), 0.585, w=W)], lv["trim"], n=10, caps=(False, False))
    for y, h, col in ((-1.0, 0.62, lv["trim"]), (1.0, 0.72, lv["accent"])):
        mb.loft([R((0, y, bz + br - 0.12), 0.36, w=W), R((0, y, bz + br + h * 0.6), 0.34, w=W), R((0, y, bz + br + h), 0.20, w=W),
                 R((0, y, bz + br + h + 0.08), 0.05, w=W)], col, n=10)
    for sx in (1, -1):
        mb.box((sx * 0.20, 0.0, bz + br + 0.30), (0.05, 0.05, 0.6), lv["trim"], w=W)
    mb.box((0, 0.0, bz + br + 0.60), (0.48, 0.05, 0.05), lv["trim"], w=W)
    B = {"bell": 1}
    mb.loft([R((0, 0, bz + br + 0.56), 0.05, w=B), R((0, 0, bz + br + 0.46), 0.12, w=B), R((0, 0, bz + br + 0.26), 0.17, w=B),
             R((0, 0, bz + br + 0.22), 0.21, w=B)], "brass", n=8)
    bones.append(("bell", "root", (0, 0, bz + br + 0.58), (0, 0, bz + br + 0.2)))
    cyl(mb, (0.25, 1.55, bz + br), (0.25, 1.55, bz + br + 0.45), 0.04, "brass", w=W)
    mb.box((0, -3.0, bz + br + 0.12), (0.5, 0.4, 0.10), "iron", w=W)
    mb.box((0, -3.05, bz + br + 0.45), (0.56, 0.5, 0.60), lv["accent"], w=W)
    mb.box((0, -3.31, bz + br + 0.45), (0.40, 0.04, 0.42), "glass_lit", w=W)
    gable(mb, (0, -3.05, bz + br + 0.75), (0.56, 0.5), 0.14, lv["trim"], w=W, overhang=0.04, ridge_along="y")
    # cab
    cz0, cz1 = 1.30, 3.55
    for sx in (1, -1):
        x = sx * 1.32
        mb.box((x, 3.55, cz0 + 0.55), (0.10, 2.0, 1.1), lv["body"], w=W)
        mb.box((x, 3.55, cz1 - 0.30), (0.10, 2.0, 0.6), lv["body"], w=W)
        for y in (2.62, 3.55, 4.48):
            mb.box((x, y, 2.75), (0.10, 0.16, 0.9), lv["body"], w=W)
        mb.box((x + sx * 0.03, 3.55, 2.32), (0.14, 2.0, 0.08), lv["trim"], w=W)
    mb.box((0, 2.60, 3.05), (2.7, 0.10, 1.0), lv["body"], w=W)
    for sx in (1, -1):
        mb.box((sx * 1.12, 2.60, 2.05), (0.5, 0.10, 1.4), lv["body"], w=W)
        mb.box((sx * 0.95, 2.56, 3.05), (0.5, 0.04, 0.5), "glass", w=W)
    mb.box((0, 3.55, cz0 + 0.04), (2.6, 2.0, 0.08), "plank_dark", w=W)
    mb.loft([R((0, 2.35, cz1), 1.55, 0.36, 0.05, w=W), R((0, 4.85, cz1), 1.55, 0.36, 0.05, w=W)], lv["roof"], n=12, p=2.2, front=(0, 0, 1))
    # cylinders, cowcatcher, pilot deck
    for sx in (1, -1):
        cyl(mb, (sx * 1.12, -3.15, DRV_R + 0.02), (sx * 1.12, -2.05, DRV_R + 0.02), 0.30, "iron", n=10, w=W, front=(0, 0, 1))
        for y in (-3.18, -2.02):
            cyl(mb, (sx * 1.12, y - 0.03, DRV_R + 0.02), (sx * 1.12, y + 0.03, DRV_R + 0.02), 0.33, lv["trim"], n=10, w=W, front=(0, 0, 1))
        mb.box((sx * 1.12, -2.6, DRV_R + 0.42), (0.40, 0.8, 0.26), "iron", w=W)
        mb.box((sx * 1.12, -1.2, DRV_R + 0.16), (0.06, 1.7, 0.06), "steel", w=W)     # slide bar
    mb.box((0, -3.75, 1.10), (2.5, 0.9, 0.12), "iron", w=W)
    mb.box((0, -4.22, 0.95), (2.5, 0.12, 0.34), lv["accent"], w=W)
    n = 9
    for k in range(n):
        t = k / (n - 1) * 2 - 1
        top = Vector((t * 1.15, -4.28, 0.95))
        tip = Vector((t * 0.12, -5.35, 0.12))
        cyl(mb, top, tip, 0.045, lv["accent"], n=4, w=W, p=4)
    cyl(mb, (-1.2, -4.30, 0.16), (-0.05, -5.40, 0.10), 0.06, lv["accent"], n=4, w=W, p=4)
    cyl(mb, (1.2, -4.30, 0.16), (0.05, -5.40, 0.10), 0.06, lv["accent"], n=4, w=W, p=4)
    coupler(mb, 4.6, 1)
    # wheels
    for i, y in enumerate(DRV_Y):
        bn = f"driver_{i + 1}"
        axle(mb, y, DRV_R, {bn: 1}, spokes=10)
        bones.append((bn, "root", (0, y, DRV_R), (0.3, y, DRV_R)))
        for sx in (1, -1):   # crank pins: left at the bottom, right at the front (quartered)
            off = Vector((0, 0, -CRANK)) if sx > 0 else Vector((0, -CRANK, 0))
            p = Vector((sx * (GAUGE + 0.20), y, DRV_R)) + off
            cyl(mb, p - Vector((sx * 0.06, 0, 0)), p + Vector((sx * 0.16, 0, 0)), 0.06, "steel", n=6, w={bn: 1}, front=(0, 0, 1))
    for i, y in enumerate((-3.35, -2.25)):
        bn = f"pilot_{i + 1}"
        axle(mb, y, 0.40, {bn: 1})
        bones.append((bn, "root", (0, y, 0.40), (0.3, y, 0.40)))
    # rods
    for sd, sx in (("l", 1), ("r", -1)):
        off = Vector((0, 0, -CRANK)) if sx > 0 else Vector((0, -CRANK, 0))
        x = sx * (GAUGE + 0.30)
        a, b = Vector((x, DRV_Y[0], DRV_R)) + off, Vector((x, DRV_Y[1], DRV_R)) + off
        S = {"side_rod_" + sd: 1}
        mb.box((a + b) / 2, (0.05, (b - a).length + 0.24, 0.13), "steel", w=S)
        bones.append(("side_rod_" + sd, "root", tuple(a), tuple(b)))
        pin = a
        ch = Vector((x + sx * 0.07, pin.y - sqrt(ROD_L ** 2 - (pin.z - CROSS_Z) ** 2), CROSS_Z))
        M = {"main_rod_" + sd: 1}
        pe = Vector((x + sx * 0.07, pin.y, pin.z))
        d = (pe - ch)
        mb.loft([R(ch, 0.025, 0.07, w=M), R(ch + d * 0.5, 0.025, 0.05, w=M), R(pe, 0.025, 0.07, w=M)], "steel", n=4, p=4, front=(0, 0, 1))
        bones.append(("main_rod_" + sd, "root", tuple(ch), tuple(pe)))
        C = {"crosshead_" + sd: 1}
        mb.box(ch, (0.12, 0.30, 0.24), "steel_dark", w=C)
        cyl(mb, ch, ch + Vector((0, -1.3, 0)), 0.035, "steel", n=6, w=C, front=(0, 0, 1))
        bones.append(("crosshead_" + sd, "root", tuple(ch), tuple(ch + Vector((0, -0.3, 0)))))
    return bones


def loco_roll(arm):
    P = Poser(arm)
    rest = {n: P.head[n].copy() for n in P.order}
    keys = []
    N = 24
    for f in range(N + 1):
        th = 2 * pi * f / N
        pose = {}
        for bn in ("driver_1", "driver_2"):
            pose[bn] = rot(x=360 * f / N)
        for bn in ("pilot_1", "pilot_2"):
            pose[bn] = rot(x=(720 * f / N) % 360)
        for sd, sx in (("l", 1), ("r", -1)):
            if sx > 0:
                off0, off = Vector((0, 0, -CRANK)), Vector((0, CRANK * sin(th), -CRANK * cos(th)))
            else:
                off0, off = Vector((0, -CRANK, 0)), Vector((0, -CRANK * cos(th), -CRANK * sin(th)))
            dl = off - off0
            pose["side_rod_" + sd] = rot(loc=tuple(dl)) if dl.length > 1e-6 else rot()
            pin = Vector((0, DRV_Y[0], DRV_R)) + off
            chy = pin.y - sqrt(ROD_L ** 2 - (pin.z - CROSS_Z) ** 2)
            dy = chy - rest["crosshead_" + sd].y
            pose["crosshead_" + sd] = rot(loc=(0, dy, 0.0001))
            pose["main_rod_" + sd] = aim((0, pin.y - chy, pin.z - CROSS_Z), fwd=(0, 0, 1), loc=(0, dy, 0.0001))
        pose["bell"] = rot(x=22 * sin(2 * th))
        keys.append((f, pose))
    return make_action(arm, P, arm.name + "_roll", keys, loop=True)


def car_roll(arm, r=0.42):
    P = Poser(arm)
    N = 24
    axles = [n for n in P.order if n.startswith("axle_")]
    keys = [(f, {a: rot(x=360 * f / N) for a in axles}) for f in range(N + 1)]
    return make_action(arm, P, arm.name + "_roll", keys, loop=True)


def door_action(arm, slide=2.25):
    P = Poser(arm)
    keys = [(0, {"door_l": rot(), "door_r": rot()}), (4, {"door_l": rot(loc=(0, -0.08, 0.0001)), "door_r": rot(loc=(0, -0.08, 0.0001))}),
            (22, {"door_l": rot(loc=(0, slide * 1.02, 0.0001)), "door_r": rot(loc=(0, slide * 1.02, 0.0001))}),
            (26, {"door_l": rot(loc=(0, slide, 0.0001)), "door_r": rot(loc=(0, slide, 0.0001))})]
    return make_action(arm, P, arm.name + "_doors_open", keys)


BUILDERS = {"Locomotive": (locomotive, {}), "Tender": (tender, {}), "PassengerCar": (passenger_car, {}), "Boxcar": (boxcar, {}),
            "Flatcar_Crates": (flatcar, {"cargo": "crates"}), "Flatcar_Gatling": (flatcar, {"cargo": "gatling"}),
            "Flatcar_Logs": (flatcar, {"cargo": "logs"}), "Caboose": (caboose, {})}
LENGTH = {"Locomotive": (5.4, 5.1), "Tender": (3.7, 3.7), "PassengerCar": (7.9, 7.9), "Boxcar": (6.0, 6.0), "Flatcar_Crates": (6.0, 6.0),
          "Flatcar_Gatling": (6.0, 6.0), "Flatcar_Logs": (6.0, 6.0), "Caboose": (5.4, 5.4)}   # (front, back) coupler reach from origin


def build_vehicle(kind, livery="player", name=None, location=(0, 0, 0)):
    fn, kw = BUILDERS[kind]
    name = name or f"Train_{kind}" + ("" if livery == "player" else "_" + livery.capitalize())
    coll = get_collection(name, get_collection("WW_Train"))
    mb = MB(name + "_mesh")
    bones = fn(mb, LIVERY[livery], **kw)
    arm = build_armature(name, [("root", None, (0, 0, 0), (0, 0.5, 0))] + bones, coll)
    ob = mb.finish(coll, arm)
    if kind == "Boxcar":
        door_action(arm)
    if kind == "Locomotive":
        loco_roll(arm)
    else:
        car_roll(arm)
    arm.location = location
    arm["ww_kind"] = "train"
    return arm, ob


def build_consist(kinds, livery, x=0.0, y_front=0.0, z=0.0, prefix=None):
    """Couple vehicles nose to tail, heading -Y. Returns [(arm, mesh)]."""
    out = []
    y = y_front
    for i, k in enumerate(kinds):
        f, b = LENGTH[k]
        y += f
        nm = None if prefix is None else f"{prefix}_{i}_{k}"
        out.append(build_vehicle(k, livery, name=nm, location=(x, y, z)))
        y += b
    return out
