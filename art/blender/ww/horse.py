"""Saddled horse: mesh, skeleton and gaits. Faces -Y like the riders. Rider attaches to the `saddle` bone."""
from math import cos, pi, radians, sin

from mathutils import Euler, Matrix, Vector

from .core import MB, R, Poser, bell, build_armature, get_collection, lerp, make_action, rot, smooth

SADDLE_Y = 0.04
SEAT_Z = 1.50
RIDER_DROP = 0.90   # rider root sits this far below the saddle bone (times the rider's scale)

# y, centre z, rx, ry up, ry down, weights
BODY = [
    (-0.68, 1.22, 0.09, 0.13, 0.15, {"chest": 1}),
    (-0.60, 1.20, 0.19, 0.25, 0.29, {"chest": 1}),
    (-0.42, 1.19, 0.27, 0.34, 0.37, {"chest": 1}),
    (-0.20, 1.18, 0.30, 0.31, 0.39, {"chest": 0.5, "spine_02": 0.5}),
    (0.05, 1.17, 0.31, 0.29, 0.39, {"spine_02": 0.5, "spine_01": 0.5}),
    (0.30, 1.19, 0.30, 0.29, 0.36, {"spine_01": 1}),
    (0.52, 1.23, 0.285, 0.28, 0.31, {"spine_01": 0.5, "pelvis": 0.5}),
    (0.74, 1.25, 0.280, 0.30, 0.31, {"pelvis": 1}),
    (0.93, 1.22, 0.215, 0.27, 0.29, {"pelvis": 1}),
    (1.03, 1.18, 0.10, 0.16, 0.16, {"pelvis": 1}),
]


def _body(y):
    if y <= BODY[0][0]:
        return BODY[0]
    for a, b in zip(BODY, BODY[1:]):
        if y <= b[0]:
            t = (y - a[0]) / (b[0] - a[0])
            return tuple(a[i] + (b[i] - a[i]) * t if not isinstance(a[i], dict) else (a[i] if t < 0.5 else b[i])
                         for i in range(6))
    return BODY[-1]


def body_rings(ys, k=1.0, add=0.0, w=None):
    out = []
    for y in ys:
        _, cz, rx, ru, rd, ww = _body(y)
        out.append(R((0, y, cz), rx * k + add, ru * k + add, rd * k + add, w=w or ww))
    return out


def skeleton():
    b = [
        ("root", None, (0, 0, 0), (0, 0.2, 0)),
        ("pelvis", "root", (0, 0.82, 1.32), (0, 0.45, 1.34)),
        ("spine_01", "pelvis", (0, 0.45, 1.34), (0, 0.05, 1.32)),
        ("spine_02", "spine_01", (0, 0.05, 1.32), (0, -0.35, 1.34)),
        ("chest", "spine_02", (0, -0.35, 1.34), (0, -0.60, 1.38)),
        ("neck_01", "chest", (0, -0.55, 1.40), (0, -0.80, 1.68)),
        ("neck_02", "neck_01", (0, -0.80, 1.68), (0, -0.98, 1.95)),
        ("head", "neck_02", (0, -0.98, 1.95), (0, -1.44, 1.70)),
        ("tail_01", "pelvis", (0, 0.99, 1.36), (0, 1.12, 1.15)),
        ("tail_02", "tail_01", (0, 1.12, 1.15), (0, 1.20, 0.85)),
        ("tail_03", "tail_02", (0, 1.20, 0.85), (0, 1.24, 0.52)),
        ("saddle", "spine_01", (0, SADDLE_Y, SEAT_Z), (0, SADDLE_Y, SEAT_Z + 0.2)),
    ]
    for sd, sx in (("l", 1), ("r", -1)):
        def m(x, y, z):
            return (x * sx, y, z)
        b += [
            ("ear_" + sd, "head", m(0.06, -0.98, 2.04), m(0.085, -0.95, 2.17)),
            ("f_upper_" + sd, "chest", m(0.16, -0.52, 1.22), m(0.17, -0.43, 0.97)),
            ("f_fore_" + sd, "f_upper_" + sd, m(0.17, -0.43, 0.97), m(0.17, -0.445, 0.56)),
            ("f_cannon_" + sd, "f_fore_" + sd, m(0.17, -0.445, 0.56), m(0.17, -0.445, 0.22)),
            ("f_pastern_" + sd, "f_cannon_" + sd, m(0.17, -0.445, 0.22), m(0.17, -0.50, 0.09)),
            ("f_hoof_" + sd, "f_pastern_" + sd, m(0.17, -0.50, 0.09), m(0.17, -0.55, 0.0)),
            ("h_thigh_" + sd, "pelvis", m(0.17, 0.66, 1.22), m(0.19, 0.54, 0.92)),
            ("h_gaskin_" + sd, "h_thigh_" + sd, m(0.19, 0.54, 0.92), m(0.19, 0.84, 0.58)),
            ("h_cannon_" + sd, "h_gaskin_" + sd, m(0.19, 0.84, 0.58), m(0.19, 0.80, 0.22)),
            ("h_pastern_" + sd, "h_cannon_" + sd, m(0.19, 0.80, 0.22), m(0.19, 0.745, 0.09)),
            ("h_hoof_" + sd, "h_pastern_" + sd, m(0.19, 0.745, 0.09), m(0.19, 0.695, 0.0)),
        ]
    return b


def build_body(mb, c):
    coat, mane, sock = c["coat"], c["mane"], c.get("sock")
    mb.loft(body_rings([b[0] for b in BODY]), coat, n=12, p=2.3, front=(0, 0, 1))
    # neck (ry = throat side, ryb = crest side)
    neck = [((0, -0.46, 1.27), 0.185, 0.27, 0.24, {"chest": 1}), ((0, -0.62, 1.46), 0.140, 0.21, 0.19, {"chest": 0.4, "neck_01": 0.6}),
            ((0, -0.78, 1.68), 0.105, 0.16, 0.15, {"neck_01": 0.5, "neck_02": 0.5}), ((0, -0.92, 1.88), 0.088, 0.125, 0.125, {"neck_02": 1}),
            ((0, -0.99, 2.00), 0.080, 0.10, 0.10, {"neck_02": 0.4, "head": 0.6})]
    mb.loft([R(p, rx, ryf, ryb, w=w) for p, rx, ryf, ryb, w in neck], coat, n=10, p=2.2)
    # head (ry = nasal side, ryb = jaw side)
    H = {"head": 1}
    mb.default_w = H
    mb.loft([R((0, -0.93, 2.02), 0.070, 0.08, 0.09), R((0, -1.05, 1.965), 0.102, 0.10, 0.145), R((0, -1.20, 1.865), 0.080, 0.078, 0.105),
             R((0, -1.34, 1.775), 0.058, 0.060, 0.072), R((0, -1.44, 1.705), 0.062, 0.062, 0.068), R((0, -1.495, 1.672), 0.040, 0.04, 0.04)],
            coat, n=10, p=2.4)
    for sx in (1, -1):
        mb.box((0.098 * sx, -1.08, 1.985), (0.014, 0.040, 0.030), "eye", rot=Euler((radians(-30), 0, 0)))
        mb.box((0.040 * sx, -1.485, 1.700), (0.018, 0.014, 0.022), "eye")
    if c.get("blaze"):
        mb.box((0, -1.22, 1.935), (0.050, 0.40, 0.012), "horse_white", rot=Euler((radians(-32), 0, 0)), taper=(1, 1))
    mb.box((0, -1.06, 2.075), (0.10, 0.16, 0.05), mane, rot=Euler((radians(-25), 0, 0)), taper=(0.6, 0.8))
    for sd, sx in (("l", 1), ("r", -1)):
        b = Vector((0.062 * sx, -0.975, 2.04))
        t = Vector((0.090 * sx, -0.945, 2.185))
        mb.loft([R(b, 0.030, 0.022, w={"ear_" + sd: 1}), R((b + t) / 2, 0.026, 0.016, w={"ear_" + sd: 1}), R(t, 0.004, 0.004, w={"ear_" + sd: 1})],
                coat, n=4)
    mb.default_w = None
    # mane along the crest
    crest = [Vector((0, -0.99, 2.08)), Vector((0, -0.84, 1.91)), Vector((0, -0.62, 1.69)), Vector((0, -0.40, 1.57))]
    N = 8
    for i in range(N):
        t = i / (N - 1) * (len(crest) - 1)
        k = min(int(t), len(crest) - 2)
        p = crest[k].lerp(crest[k + 1], t - k)
        d = (crest[k + 1] - crest[k]).normalized()
        ang = -Vector((0, 1)).angle_signed(Vector((d.y, d.z)))
        w = {"neck_02": 1} if t < 1 else ({"neck_01": 1} if t < 2.2 else {"chest": 1})
        mb.box(p + Vector((0.035, 0, -0.04)), (0.06, 0.15, 0.17), mane, w=w, rot=Euler((ang, 0, radians(-8))), taper=(0.5, 0.9))
    # tail
    mb.loft([R((0, 0.98, 1.37), 0.045, w={"pelvis": 1}), R((0, 1.10, 1.22), 0.070, w={"tail_01": 1}),
             R((0, 1.17, 0.98), 0.085, w={"tail_01": 0.5, "tail_02": 0.5}), R((0, 1.21, 0.78), 0.080, w={"tail_02": 0.5, "tail_03": 0.5}),
             R((0, 1.24, 0.52), 0.030, w={"tail_03": 1})], mane, n=6)
    for mirror in (False, True):
        mb.mirror = mirror
        lower = sock or coat
        fu, ff, fc, fp, fh = "f_upper_l", "f_fore_l", "f_cannon_l", "f_pastern_l", "f_hoof_l"
        mb.loft([R((0.165, -0.50, 1.27), 0.105, 0.14, w={"chest": 0.7, fu: 0.3}), R((0.17, -0.44, 1.02), 0.080, 0.105, w={fu: 1}),
                 R((0.17, -0.43, 0.95), 0.072, 0.090, w={fu: 0.5, ff: 0.5}), R((0.17, -0.44, 0.64), 0.046, 0.052, w={ff: 1}),
                 R((0.17, -0.447, 0.56), 0.052, 0.057, w={ff: 0.5, fc: 0.5}), R((0.17, -0.445, 0.49), 0.039, 0.042, w={fc: 1}, mat=lower),
                 R((0.17, -0.445, 0.28), 0.034, 0.037, w={fc: 1}, mat=lower), R((0.17, -0.447, 0.22), 0.044, 0.049, w={fc: 0.5, fp: 0.5}, mat=lower),
                 R((0.17, -0.495, 0.10), 0.042, 0.046, w={fp: 1}, mat=lower)], coat, n=8)
        mb.loft([R((0.17, -0.495, 0.105), 0.048, 0.052, w={fp: 0.5, fh: 0.5}), R((0.17, -0.525, 0.0), 0.062, 0.072, 0.060, w={fh: 1})],
                "hoof", n=8)
        ht, hg, hc, hp, hh = "h_thigh_l", "h_gaskin_l", "h_cannon_l", "h_pastern_l", "h_hoof_l"
        mb.loft([R((0.16, 0.68, 1.30), 0.125, 0.22, 0.20, w={"pelvis": 0.7, ht: 0.3}), R((0.18, 0.60, 1.04), 0.105, 0.17, 0.15, w={ht: 1}),
                 R((0.19, 0.555, 0.92), 0.085, 0.12, 0.13, w={ht: 0.5, hg: 0.5}), R((0.19, 0.70, 0.74), 0.056, 0.075, w={hg: 1}),
                 R((0.19, 0.84, 0.58), 0.049, 0.060, 0.066, w={hg: 0.5, hc: 0.5}), R((0.19, 0.828, 0.50), 0.039, 0.044, w={hc: 1}, mat=lower),
                 R((0.19, 0.805, 0.28), 0.035, 0.038, w={hc: 1}, mat=lower), R((0.19, 0.80, 0.22), 0.044, 0.049, w={hc: 0.5, hp: 0.5}, mat=lower),
                 R((0.19, 0.75, 0.10), 0.042, 0.046, w={hp: 1}, mat=lower)], coat, n=8)
        mb.loft([R((0.19, 0.75, 0.105), 0.048, 0.052, w={hp: 0.5, hh: 0.5}), R((0.19, 0.72, 0.0), 0.062, 0.072, 0.060, w={hh: 1})],
                "hoof", n=8)
    mb.mirror = False


def build_tack(mb, c):
    blanket, leather = c.get("blanket", "red"), c.get("saddle", "leather")
    S1 = {"spine_01": 0.6, "spine_02": 0.4}
    mb.loft(body_rings([-0.24, -0.05, 0.15, 0.36], add=0.012, w=S1), blanket, n=12, arc=(-100, 100), thick=-0.016, p=2.3, front=(0, 0, 1))
    mb.loft(body_rings([-0.16, -0.02, 0.14, 0.28], add=0.030, w=S1), leather, n=10, arc=(-72, 72), thick=-0.030, p=2.3, front=(0, 0, 1))
    # seat, pommel, horn, cantle
    mb.box((0, 0.05, 1.495), (0.26, 0.36, 0.05), leather, w=S1, taper=(0.8, 0.9))
    mb.box((0, -0.15, 1.545), (0.16, 0.08, 0.12), leather, w=S1, rot=Euler((radians(-15), 0, 0)), taper=(0.6, 0.8))
    mb.loft([R((0, -0.17, 1.58), 0.020, w=S1), R((0, -0.19, 1.65), 0.018, w=S1), R((0, -0.195, 1.665), 0.034, w=S1), R((0, -0.20, 1.68), 0.030, w=S1)],
            "leather_dark", n=6)
    mb.box((0, 0.25, 1.555), (0.30, 0.06, 0.15), leather, w=S1, rot=Euler((radians(18), 0, 0)), taper=(0.75, 0.8))
    # cinch
    lo, hi = body_rings([-0.03, 0.05], add=0.020, w=S1)
    inner = body_rings([-0.03, 0.05], add=0.004, w=S1)
    mb.loft([inner[0], lo, hi, inner[1]], "leather_dark", n=12, caps=(False, False), p=2.3, front=(0, 0, 1))
    # bedroll behind the cantle
    mb.loft([R((-0.24, 0.37, 1.545), 0.062, w=S1), R((0.24, 0.37, 1.545), 0.062, w=S1)], c.get("bedroll", "tan"), n=8)
    for x in (-0.13, 0.13):
        mb.loft([R((x - 0.012, 0.37, 1.545), 0.066, w=S1), R((x + 0.012, 0.37, 1.545), 0.066, w=S1)], "leather_dark", n=8, caps=(False, False))
    for sx in (1, -1):
        top, bot = Vector((0.315 * sx, SADDLE_Y - 0.06, 1.34)), Vector((0.385 * sx, SADDLE_Y - 0.22, 0.95))
        mb.loft([R(top, 0.008, 0.028, w=S1), R(bot, 0.008, 0.024, w=S1)], "leather_dark", n=4, p=4)
        mb.box(bot + Vector((0.0, -0.02, -0.035)), (0.075, 0.13, 0.025), "wood", w=S1)
        mb.box(bot + Vector((0.0, -0.02, 0.02)), (0.085, 0.020, 0.10), "wood", w=S1)
    # bridle
    H = {"head": 1}
    mb.loft([R((0, -1.335, 1.779), 0.050, 0.052, 0.066, w=H), R((0, -1.36, 1.760), 0.064, 0.066, 0.079, w=H),
             R((0, -1.385, 1.742), 0.064, 0.066, 0.079, w=H), R((0, -1.41, 1.726), 0.050, 0.052, 0.066, w=H)],
            "leather_dark", n=10, caps=(False, False), p=2.4)
    mb.loft([R((0, -1.035, 1.975), 0.090, 0.090, 0.135, w=H), R((0, -1.05, 1.965), 0.109, 0.108, 0.153, w=H),
             R((0, -1.075, 1.950), 0.107, 0.106, 0.150, w=H), R((0, -1.09, 1.940), 0.088, 0.088, 0.130, w=H)],
            "leather_dark", n=10, caps=(False, False), p=2.4)
    for sx in (1, -1):
        mb.loft([R((0.104 * sx, -1.07, 1.93), 0.006, 0.016, w=H), R((0.070 * sx, -1.37, 1.735), 0.006, 0.016, w=H)], "leather_dark", n=4, p=4)
        mb.loft([R((0.072 * sx, -1.375, 1.72), 0.014, w=H), R((0.076 * sx, -1.375, 1.72), 0.014, w=H)], "steel", n=6)
        pts = [((0.074 * sx, -1.375, 1.715), H), ((0.135 * sx, -1.05, 1.60), {"neck_02": 1}), ((0.165 * sx, -0.66, 1.58), {"neck_01": 1}),
               ((0.080 * sx, -0.36, 1.66), {"chest": 0.5, "spine_02": 0.5}), ((0.0, -0.22, 1.70), {"spine_02": 1})]
        mb.loft([R(p, 0.006, 0.012, w=w) for p, w in pts], "leather_dark", n=4, p=4)


def build_horse(name, c, location=(0, 0, 0)):
    coll = get_collection(name, get_collection("WW_Horses"))
    arm = build_armature(name, skeleton(), coll)
    parts = []
    for suffix, fn in (("body", build_body), ("tack", build_tack)):
        mb = MB(f"{name}_{suffix}")
        fn(mb, c)
        parts.append(mb.finish(coll, arm))
    arm.location = location
    arm["ww_kind"] = "horse"
    return arm, parts


def mount(rider, horse, scale=1.0):
    """Preview only: pin the rider's root under the horse's saddle bone."""
    con = rider.constraints.new("CHILD_OF")
    con.target = horse
    con.subtarget = "saddle"
    bone_rest = horse.matrix_world @ horse.data.bones["saddle"].matrix_local
    want = horse.matrix_world @ Matrix.Translation((0, SADDLE_Y, SEAT_Z - RIDER_DROP * scale))
    rider.location = (0, 0, 0)
    rider.matrix_world = Matrix.Identity(4)
    con.inverse_matrix = bone_rest.inverted() @ want
    return con


# ---------------------------------------------------------------- gaits

def _cycle(u, sf):
    """Swing angle factor (-1 forward .. +1 back), fold 0..1."""
    u %= 1.0
    if u < sf:
        return lerp(-1, 1, u / sf), 0.0
    t = (u - sf) / (1 - sf)
    return lerp(1, -1, smooth(t)), bell(min(1.0, t * 1.15))


def front_leg(sd, u, sf, A, K, load=1.0):
    a, f = _cycle(u, sf)
    ang = a * A
    ld = (1 - abs(a)) * load if f == 0 else 0
    return {f"f_upper_{sd}": rot(ang * 0.55 - f * K * 0.30), f"f_fore_{sd}": rot(ang * 0.45 - f * K * 0.40),
            f"f_cannon_{sd}": rot(f * K + ld * 2), f"f_pastern_{sd}": rot(f * K * 0.55 - ld * 16 - ang * 0.5), f"f_hoof_{sd}": rot(f * K * 0.25)}


def hind_leg(sd, u, sf, A, K, load=1.0):
    a, f = _cycle(u, sf)
    ang = a * A
    ld = (1 - abs(a)) * load if f == 0 else 0
    return {f"h_thigh_{sd}": rot(ang * 0.70 - f * K * 0.45 + ld * 4), f"h_gaskin_{sd}": rot(ang * 0.30 + f * K * 0.80 - ld * 10),
            f"h_cannon_{sd}": rot(-f * K * 0.95 + ld * 10), f"h_pastern_{sd}": rot(f * K * 0.6 - ld * 16 - ang * 0.5), f"h_hoof_{sd}": rot(f * K * 0.25)}


def p_gallop(ph):
    c9 = cos(2 * pi * (ph - 0.90))
    pitch = -5.5 * cos(2 * pi * (ph - 0.05))
    d = {"pelvis": rot(pitch, loc=(0, 0, 0.07 * c9 - 0.03)), "spine_01": rot(4.5 * c9), "spine_02": rot(4.0 * c9), "chest": rot(3.0 * c9),
         "neck_01": rot(16 - pitch * 0.8 + 5 * cos(2 * pi * (ph - 0.5))), "neck_02": rot(4 - 3 * c9), "head": rot(-14 + 4 * sin(2 * pi * ph)),
         "tail_01": rot(58 + 8 * sin(2 * pi * ph)), "tail_02": rot(14 + 10 * sin(2 * pi * ph - 1.0)), "tail_03": rot(10 + 12 * sin(2 * pi * ph - 2.0)),
         "ear_l": rot(35), "ear_r": rot(35)}
    sf = 0.27
    d.update(hind_leg("l", ph - 0.00, sf, 34, 78))
    d.update(hind_leg("r", ph - 0.10, sf, 34, 78))
    d.update(front_leg("l", ph - 0.40, sf, 36, 100))
    d.update(front_leg("r", ph - 0.52, sf, 36, 100))
    return d


def p_walk(ph):
    d = {"pelvis": rot(0.8 * sin(4 * pi * ph), 0, 1.5 * sin(2 * pi * ph), loc=(0, 0, -0.012 * cos(4 * pi * ph))),
         "spine_02": rot(0, 0, -1.5 * sin(2 * pi * ph)), "neck_01": rot(6 + 4 * sin(4 * pi * ph)), "neck_02": rot(2), "head": rot(-4 - 3 * sin(4 * pi * ph)),
         "tail_01": rot(6, 8 * sin(2 * pi * ph), 0), "tail_02": rot(0, 8 * sin(2 * pi * ph - 1), 0), "tail_03": rot(0, 8 * sin(2 * pi * ph - 2), 0)}
    sf = 0.62
    d.update(hind_leg("l", ph, sf, 17, 46, 0.4))
    d.update(front_leg("l", ph - 0.25, sf, 17, 58, 0.4))
    d.update(hind_leg("r", ph - 0.5, sf, 17, 46, 0.4))
    d.update(front_leg("r", ph - 0.75, sf, 17, 58, 0.4))
    return d


def p_idle(t):
    s = sin(2 * pi * t)
    return {"pelvis": rot(0, 0, 0, loc=(0, 0, -0.004 * s)), "spine_02": rot(0.6 * s), "neck_01": rot(5 + 2 * s), "neck_02": rot(2), "head": rot(-5 + 2 * sin(2 * pi * t + 1)),
            "tail_01": rot(4, 14 * sin(4 * pi * t), 0), "tail_02": rot(0, 18 * sin(4 * pi * t - 1), 0), "tail_03": rot(0, 20 * sin(4 * pi * t - 2), 0),
            "ear_l": rot(10 * bell(t * 3 % 1) if t < 0.34 else 0, 0, 0), "ear_r": rot(0, 0, 12 * bell((t - 0.5) * 3) if 0.5 < t < 0.84 else 0),
            "h_thigh_r": rot(-4), "h_gaskin_r": rot(10), "h_cannon_r": rot(-14), "h_pastern_r": rot(22)}


def p_rear(k):
    """k 0..1: up on the hind legs."""
    a = -40 * k
    d = {"pelvis": rot(a, loc=(0, 0.10 * k, -0.06 * k)), "spine_01": rot(-4 * k), "spine_02": rot(-4 * k), "neck_01": rot(14 * k), "head": rot(18 * k),
         "tail_01": rot(20 * k)}
    for sd, o in (("l", 0), ("r", 14)):
        d.update({f"h_thigh_{sd}": rot(-a * 0.75 - 8 * k), f"h_gaskin_{sd}": rot(-a * 0.25 + 16 * k), f"h_cannon_{sd}": rot(-16 * k), f"h_pastern_{sd}": rot(8 * k),
                  f"f_upper_{sd}": rot((-38 - o) * k), f"f_fore_{sd}": rot((-30 + o) * k), f"f_cannon_{sd}": rot((105 - o * 2) * k), f"f_pastern_{sd}": rot(50 * k)})
    return d


def death():
    fold = lambda k: merge_legs(k)
    k0 = p_idle(0)
    k1 = dict(p_rear(0.55))
    k2 = {"pelvis": rot(10, 18, 0, loc=(0.10, 0, -0.30)), "neck_01": rot(28), "head": rot(10), "tail_01": rot(20)}
    k2.update(merge_legs(0.7))
    k3 = {"pelvis": rot(0, 84, 0, loc=(0.55, 0, -0.98)), "spine_02": rot(0, 0, 6), "neck_01": rot(10, 14, 0), "neck_02": rot(0, 12, 0), "head": rot(-10, 10, 0),
          "tail_01": rot(30)}
    k3.update(merge_legs(0.35))
    k4 = dict(k3)
    k4.update({"pelvis": rot(0, 88, 0, loc=(0.58, 0, -1.00)), "neck_01": rot(6, 20, 0)})
    k4.update(merge_legs(0.22))
    return [(0, k0), (10, k1), (20, k2), (32, k3), (38, k4), (50, k4)]


def merge_legs(k):
    d = {}
    for sd, j in (("l", 1.0), ("r", 0.8)):
        q = k * j
        d.update({f"f_upper_{sd}": rot(-30 * q), f"f_fore_{sd}": rot(-35 * q), f"f_cannon_{sd}": rot(120 * q), f"f_pastern_{sd}": rot(40 * q),
                  f"h_thigh_{sd}": rot(-30 * q), f"h_gaskin_{sd}": rot(60 * q), f"h_cannon_{sd}": rot(-80 * q), f"h_pastern_{sd}": rot(40 * q)})
    return d


def build_horse_actions(arm):
    P = Poser(arm)
    A = {}

    def add(name, keys, loop=False):
        A[name] = make_action(arm, P, "horse_" + name, keys, loop)
    add("idle", [(f, p_idle(f / 60)) for f in range(0, 61, 3)], loop=True)
    add("walk", [(f, p_walk(f / 40)) for f in range(0, 41, 2)], loop=True)
    add("gallop", [(f, p_gallop(f / 14)) for f in range(0, 15)], loop=True)
    add("rear", [(0, p_idle(0)), (10, p_rear(1.0)), (16, p_rear(0.92)), (22, p_rear(1.0)), (36, p_idle(0))])
    add("death", death())
    return A
