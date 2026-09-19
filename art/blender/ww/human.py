"""Modular cowboy: one skeleton, swappable hat / face / outerwear / weapon / palette."""
from math import cos, exp, pi, radians, sin

from mathutils import Euler, Matrix, Vector

from . import props
from .core import MB, R, build_armature, get_collection

# ---------------------------------------------------------------- landmarks (unscaled metres, facing -Y, left = +X)
ARM_D = Vector((cos(radians(50)), 0, -sin(radians(50))))
ARM_N = Vector((sin(radians(50)), 0, cos(radians(50))))  # back-of-hand side
SHO = Vector((0.19, 0, 1.46))
ELB = SHO + ARM_D * 0.29
WRI = ELB + ARM_D * 0.27
KNU = WRI + ARM_D * 0.095
GRIP = WRI + ARM_D * 0.070 - ARM_N * 0.034
HIP = Vector((0.105, 0, 0.95))
KNEE = Vector((0.105, 0, 0.52))
ANK = Vector((0.105, 0, 0.10))
FINGERS = (("index", -0.033, 0.036, 0.030), ("middle", -0.011, 0.040, 0.032), ("ring", 0.011, 0.037, 0.030),
           ("pinky", 0.032, 0.030, 0.025))
THUMB_O = WRI + ARM_D * 0.030 + Vector((0, -0.040, 0)) - ARM_N * 0.006
THUMB_D = (ARM_D * 0.65 + Vector((0, -0.75, 0))).normalized()


def skeleton(cfg=None):
    V = Vector
    tip = muzzle_tip(cfg or {})
    b = [
        ("root", None, (0, 0, 0), (0, 0.15, 0)),
        ("pelvis", "root", (0, 0, 0.98), (0, 0, 1.08)),
        ("spine_01", "pelvis", (0, 0, 1.08), (0, 0, 1.20)),
        ("spine_02", "spine_01", (0, 0, 1.20), (0, 0, 1.34)),
        ("spine_03", "spine_02", (0, 0, 1.34), (0, 0, 1.50)),
        ("neck_01", "spine_03", (0, 0, 1.50), (0, -0.01, 1.60)),
        ("head", "neck_01", (0, -0.01, 1.60), (0, -0.01, 1.84)),
        ("hat", "head", (0, -0.01, 1.80), (0, -0.01, 1.95)),
    ]
    for sd, sx in (("l", 1), ("r", -1)):
        def m(v):
            return (v[0] * sx, v[1], v[2])
        b += [
            ("clavicle_" + sd, "spine_03", m((0.03, 0, 1.46)), m(SHO)),
            ("upperarm_" + sd, "clavicle_" + sd, m(SHO), m(ELB)),
            ("lowerarm_" + sd, "upperarm_" + sd, m(ELB), m(WRI)),
            ("hand_" + sd, "lowerarm_" + sd, m(WRI), m(KNU)),
            ("weapon_" + sd, "hand_" + sd, m(GRIP), m(GRIP + ARM_D * 0.10)),
            ("muzzle_" + sd, "weapon_" + sd, m(tip), m(tip + ARM_D * 0.08)),
            ("thumb_01_" + sd, "hand_" + sd, m(THUMB_O), m(THUMB_O + THUMB_D * 0.036)),
            ("thumb_02_" + sd, "thumb_01_" + sd, m(THUMB_O + THUMB_D * 0.036), m(THUMB_O + THUMB_D * 0.068)),
        ]
        for fn, yo, l1, l2 in FINGERS:
            k = KNU + V((0, yo, 0))
            b += [(f"{fn}_01_{sd}", "hand_" + sd, m(k), m(k + ARM_D * l1)),
                  (f"{fn}_02_{sd}", f"{fn}_01_{sd}", m(k + ARM_D * l1), m(k + ARM_D * (l1 + l2)))]
        b += [
            ("thigh_" + sd, "pelvis", m(HIP), m(KNEE)),
            ("calf_" + sd, "thigh_" + sd, m(KNEE), m(ANK)),
            ("foot_" + sd, "calf_" + sd, m(ANK), m((0.105, -0.16, 0.035))),
            ("ball_" + sd, "foot_" + sd, m((0.105, -0.16, 0.035)), m((0.105, -0.27, 0.03))),
        ]
    return b


# ---------------------------------------------------------------- shape tables
# z, rx, ry_front, ry_back, weights
TORSO = [
    (0.88, 0.120, 0.095, 0.095, {"pelvis": 1}),
    (0.93, 0.178, 0.122, 0.126, {"pelvis": 1}),
    (1.00, 0.186, 0.126, 0.128, {"pelvis": 1}),
    (1.04, 0.176, 0.122, 0.120, {"pelvis": 0.6, "spine_01": 0.4}),
    (1.12, 0.170, 0.118, 0.114, {"spine_01": 1}),
    (1.22, 0.182, 0.128, 0.120, {"spine_01": 0.5, "spine_02": 0.5}),
    (1.34, 0.208, 0.146, 0.130, {"spine_02": 0.5, "spine_03": 0.5}),
    (1.43, 0.226, 0.138, 0.128, {"spine_03": 1}),
    (1.495, 0.190, 0.108, 0.108, {"spine_03": 1}),
    (1.535, 0.085, 0.078, 0.078, {"spine_03": 0.6, "neck_01": 0.4}),
]
# z, y, rx, ry_front, ry_back
HEAD = [
    (1.580, -0.012, 0.044, 0.058, 0.040),
    (1.608, -0.016, 0.060, 0.094, 0.062),
    (1.648, -0.014, 0.074, 0.100, 0.086),
    (1.700, -0.010, 0.082, 0.100, 0.100),
    (1.750, -0.010, 0.082, 0.104, 0.104),
    (1.800, -0.006, 0.078, 0.092, 0.100),
    (1.840, -0.004, 0.056, 0.062, 0.072),
]


HEAD_S = 1.16
HEAD_PIV = Vector((0, -0.01, 1.615))
HEAD_XF = Matrix.Translation(HEAD_PIV) @ Matrix.Scale(HEAD_S, 4) @ Matrix.Translation(-HEAD_PIV)


def _interp(table, z):
    if z <= table[0][0]:
        return table[0]
    for a, b in zip(table, table[1:]):
        if z <= b[0]:
            t = (z - a[0]) / (b[0] - a[0])
            return tuple(a[i] + (b[i] - a[i]) * t if not isinstance(a[i], dict) else (a[i] if t < 0.5 else b[i])
                         for i in range(len(a)))
    return table[-1]


def torso_rings(zs, k=1.0, g=1.0, add=0.0, **kw):
    out = []
    for z in zs:
        _, rx, ryf, ryb, w = _interp(TORSO, z)
        out.append(R((0, 0, z), rx * g * k + add, ryf * g * k + add, ryb * g * k + add, w=w, **kw))
    return out


def head_rings(zs, k=1.0, add=0.0, w=None, **kw):
    out = []
    for z in zs:
        _, y, rx, ryf, ryb = _interp(HEAD, z)
        out.append(R((0, y, z), rx * k + add, ryf * k + add, ryb * k + add, w=w or {"head": 1}, **kw))
    return out


def band(mb, rings_in, mat, n, **kw):
    """Closed strap: rings_in = [bottom, top] outer rings; adds inner ones so it has thickness."""
    lo, hi = rings_in[0], rings_in[-1]
    t = 0.012

    def inner(r):
        q = dict(r)
        q["rx"] = r["rx"] - t
        q["ry"] = r["ry"] - t
        q["ryb"] = None if r["ryb"] is None else r["ryb"] - t
        return q
    mb.loft([inner(lo)] + list(rings_in) + [inner(hi)], mat, n=n, caps=(False, False), **kw)


# ---------------------------------------------------------------- parts

def build_body(mb, c):
    g = c.get("girth", 1.0)
    skin, shirt, pants, boots = c["skin"], c["shirt"], c["pants"], c["boots"]
    sleeve = c.get("sleeve", shirt)
    # torso
    rings = torso_rings([t[0] for t in TORSO], g=g)
    for r in rings:
        r["mat"] = pants if r["c"].z < 1.03 else shirt
    mb.loft(rings, shirt, n=14, p=2.5)
    # shirt placket + buttons
    if not c.get("no_buttons"):
        for z in (1.12, 1.20, 1.28, 1.36):
            _, rx, ryf, _, w = _interp(TORSO, z)
            mb.box((0, -ryf * g - 0.002, z), (0.016, 0.008, 0.016), c.get("button", "bone"), w=w)
    # neck
    mb.loft([R((0, 0, 1.49), 0.056, w={"spine_03": 0.5, "neck_01": 0.5}), R((0, -0.004, 1.56), 0.052, w={"neck_01": 1}),
             R((0, -0.010, 1.63), 0.055, w={"neck_01": 0.4, "head": 0.6})], skin, n=10)
    build_head(mb, c)
    for mirror in (False, True):
        mb.mirror = mirror
        build_arm(mb, c, g, sleeve, skin)
        build_leg(mb, c, g, pants, boots)
    mb.mirror = False


def build_head(mb, c):
    skin, hair = c["skin"], c.get("hair", "hair_brown")
    W = {"head": 1}
    mb.default_w = W
    mb.xf = HEAD_XF
    mb.loft(head_rings([h[0] for h in HEAD]), skin, n=12, p=2.35)
    # nose
    mb.poly([(0, -0.100, 1.752), (0, -0.158, 1.690), (0.025, -0.096, 1.672), (-0.025, -0.096, 1.672),
             (0, -0.110, 1.668)],
            [(0, 2, 1), (0, 1, 3), (1, 2, 4), (1, 4, 3)], skin)
    # brow ridge + eyes
    for sx in (1, -1):
        mb.box((0.036 * sx, -0.100, 1.752), (0.058, 0.026, 0.016), hair, rot=Euler((radians(-12), 0, radians(-10 * sx))))
        mb.box((0.036 * sx, -0.101, 1.730), (0.030, 0.012, 0.012), "eye")
        mb.box((0.086 * sx, 0.004, 1.706), (0.016, 0.032, 0.048), skin, rot=Euler((0, 0, radians(-14 * sx))))
    # hair
    style = c.get("hair_style", "short")
    if style != "bald":
        zlo = 1.585 if style == "long" else 1.655
        mb.loft(head_rings([zlo, 1.70, 1.76, 1.815], k=1.06, add=0.004), hair, n=12, arc=(68, 292), thick=0.014, p=2.35)
        for sx in (1, -1):  # sideburns
            mb.box((0.080 * sx, -0.040, 1.675), (0.012, 0.030, 0.060), hair)
    beard = c.get("beard", "none")
    if beard in ("full", "big"):
        zl = 1.545 if beard == "big" else 1.575
        k = 1.14 if beard == "big" else 1.08
        mb.loft(head_rings([zl, 1.60, 1.645, 1.685], k=k, add=0.004), hair, n=12, arc=(-112, 112), thick=0.016, p=2.35)
        mb.box((0, -0.105, zl + 0.02), (0.07, 0.04, 0.05), hair, taper=(1.3, 1.0))
    if beard == "goatee":
        mb.box((0, -0.108, 1.610), (0.044, 0.030, 0.050), hair, taper=(1.3, 1.0))
    if beard in ("full", "big", "mustache", "goatee", "handlebar"):
        for sx in (1, -1):
            L = 0.052 if beard == "handlebar" else 0.040
            mb.box((0.024 * sx, -0.112, 1.660), (L, 0.020, 0.016), hair,
                   rot=Euler((0, radians(18 * sx), radians(-12 * sx))), taper=(0.7, 1.0))
            if beard == "handlebar":
                mb.box((0.056 * sx, -0.104, 1.664), (0.012, 0.014, 0.026), hair, rot=Euler((0, radians(-20 * sx), 0)))
    if c.get("mask"):
        def bump(a):
            return 1.0 + 0.16 * exp(-(((a + pi) % (2 * pi) - pi) / 0.38) ** 2)
        rs = head_rings([1.590, 1.63, 1.68, 1.722], k=1.10, add=0.012, rfn=bump)
        band(mb, rs, c["mask"], n=12, p=2.35)
        mb.poly([(0.07, -0.085, 1.60), (-0.07, -0.085, 1.60), (0, -0.125, 1.47), (0, -0.075, 1.60)],
                [(0, 2, 3), (3, 2, 1), (0, 1, 2)], c["mask"], w={"head": 0.5, "neck_01": 0.5})
        mb.box((0.03, 0.125, 1.665), (0.05, 0.03, 0.04), c["mask"], rot=Euler((0, 0, radians(30))))
    if c.get("cigar"):
        mb.loft([R((0.022, -0.105, 1.640), 0.007), R((0.040, -0.175, 1.632), 0.008)], "wood_dark", n=6, front=(0, 0, 1))
        mb.box((0.041, -0.178, 1.632), (0.013, 0.008, 0.013), "spark")
    if c.get("eyepatch"):
        mb.box((0.036, -0.106, 1.730), (0.042, 0.010, 0.036), "black")
        mb.box((0, -0.060, 1.762), (0.180, 0.120, 0.008), "black", rot=Euler((0, radians(14), 0)))
    mb.default_w = None
    mb.xf = None


def build_arm(mb, c, g, sleeve, skin):
    ua, la, cl = "upperarm_l", "lowerarm_l", "clavicle_l"
    rolled = c.get("sleeves") == "rolled"
    glove = c.get("gloves")
    pts = [(-0.05, 0.074, {cl: 0.6, ua: 0.4}), (0.025, 0.082, {ua: 1}), (0.14, 0.068, {ua: 1}), (0.26, 0.058, {ua: 1}),
           (0.29, 0.057, {ua: 0.5, la: 0.5}), (0.32, 0.058, {la: 1}), (0.40, 0.055, {la: 1})]
    rs = [R(SHO + ARM_D * t, r * g, r * g * 1.05, w=w) for t, r, w in pts]
    if rolled:
        rs.append(R(SHO + ARM_D * 0.405, 0.061 * g, 0.064 * g, w={la: 1}))
        mb.loft(rs, sleeve, n=10)
        mb.loft([R(SHO + ARM_D * 0.39, 0.050 * g, 0.052 * g, w={la: 1}), R(SHO + ARM_D * 0.56, 0.038, 0.042, w={la: 1})],
                skin, n=10)
    else:
        rs += [R(SHO + ARM_D * 0.53, 0.046 * g, 0.049 * g, w={la: 1}), R(SHO + ARM_D * 0.55, 0.048 * g, 0.051 * g, w={la: 1})]
        mb.loft(rs, sleeve, n=10)
    hm = glove or skin
    if glove:
        mb.loft([R(SHO + ARM_D * 0.46, 0.060, 0.064, w={la: 1}), R(SHO + ARM_D * 0.565, 0.046, 0.050, w={la: 1})], glove, n=10)
    H = {"hand_l": 1}
    mb.loft([R(WRI - ARM_D * 0.012, 0.021, 0.034, w=H), R(WRI + ARM_D * 0.045, 0.024, 0.052, w=H),
             R(KNU + ARM_D * 0.004, 0.019, 0.050, w=H)], hm, n=8, p=3.2)
    for fn, yo, l1, l2 in FINGERS:
        k = KNU + Vector((0, yo, 0))
        mb.loft([R(k - ARM_D * 0.006, 0.0125, 0.0120), R(k + ARM_D * l1, 0.0115, 0.0115)], hm, n=4, p=4,
                w={f"{fn}_01_l": 1})
        mb.loft([R(k + ARM_D * (l1 - 0.004), 0.0115, 0.0115), R(k + ARM_D * (l1 + l2), 0.009, 0.0095)], hm, n=4, p=4,
                w={f"{fn}_02_l": 1})
    mb.loft([R(THUMB_O - THUMB_D * 0.012, 0.016, 0.016), R(THUMB_O + THUMB_D * 0.036, 0.0135, 0.0135)], hm, n=4, p=4,
            w={"thumb_01_l": 1})
    mb.loft([R(THUMB_O + THUMB_D * 0.032, 0.0135, 0.0135), R(THUMB_O + THUMB_D * 0.068, 0.011, 0.011)], hm, n=4, p=4,
            w={"thumb_02_l": 1})


def build_leg(mb, c, g, pants, boots):
    th, ca, ft, bl = "thigh_l", "calf_l", "foot_l", "ball_l"
    x = 0.105
    leg = [(0.99, 0.090, 0.104, {"pelvis": 1}), (0.90, 0.096, 0.110, {"pelvis": 0.4, th: 0.6}),
           (0.80, 0.092, 0.102, {th: 1}), (0.57, 0.074, 0.082, {th: 1}), (0.52, 0.071, 0.080, {th: 0.5, ca: 0.5}),
           (0.47, 0.069, 0.078, {ca: 1}), (0.34, 0.068, 0.078, {ca: 1}), (0.20, 0.074, 0.086, {ca: 1}),
           (0.145, 0.078, 0.092, {ca: 0.7, ft: 0.3})]
    gl = 1 + (g - 1) * 0.6
    mb.loft([R((x * gl, 0, z), rx * gl, ry * gl, w=w) for z, rx, ry, w in leg], pants, n=10, p=2.3)
    if c.get("kneepads"):
        mb.box((x * gl, -0.070 * gl, 0.525), (0.085, 0.030, 0.10), c["kneepads"], w={th: 0.5, ca: 0.5},
               taper=(0.8, 1.0))
    xx = x * gl
    mb.loft([R((xx, 0, 0.30), 0.055, 0.062, w={ca: 1}), R((xx, 0, 0.20), 0.052, 0.060, w={ca: 1}),
             R((xx, 0.004, 0.115), 0.050, 0.062, w={ca: 0.5, ft: 0.5})], boots, n=10, caps=(True, False))
    F = {ft: 1}
    mb.loft([R((xx, 0.086, 0.072), 0.038, 0.044, w=F), R((xx, 0.025, 0.090), 0.052, 0.078, 0.074, w=F),
             R((xx, -0.070, 0.056), 0.058, 0.052, 0.050, w=F), R((xx, -0.160, 0.040), 0.055, 0.036, 0.034, w={ft: 0.5, bl: 0.5}),
             R((xx, -0.235, 0.030), 0.038, 0.025, 0.024, w={bl: 1}), R((xx, -0.280, 0.024), 0.014, 0.013, w={bl: 1})],
            boots, n=10, p=2.8, front=(0, 0, 1))
    mb.box((xx, 0.050, 0.019), (0.072, 0.070, 0.038), c.get("heel", "brown_dark"), w=F, taper=(1.0, 1.0))
    if c.get("spurs"):
        mb.box((xx, 0.095, 0.060), (0.052, 0.036, 0.010), "steel", w=F)
        mb.loft([R((xx - 0.004, 0.125, 0.056), 0.018), R((xx + 0.004, 0.125, 0.056), 0.018)], "brass", n=6, w=F)


def build_hat(mb, c):
    h = dict(crown_h=0.125, crx=0.090, cry=0.106, top=0.82, brim=0.115, curl=0.050, dip=0.018, dent=0.030, pinch=0.22,
             color="brown_dark", band="leather_light", tilt=-6, round=False)
    h.update(c.get("hat") or {})
    z0 = 1.792
    piv = Vector((0, -0.005, z0))
    mb.xf = HEAD_XF @ (Matrix.Translation(piv) @ Euler((radians(h["tilt"]), 0, 0)).to_matrix().to_4x4() @ Matrix.Translation(-piv))
    mb.default_w = {"hat": 1}
    cx, cy, ch, bw = h["crx"], h["cry"], h["crown_h"], h["brim"]

    def zf(a):
        return h["curl"] * sin(a) ** 2 - h["dip"] * cos(a) ** 2

    def pin(a):
        return 1.0 - h["pinch"] * max(0.0, cos(a)) ** 2
    C = lambda z: (0, -0.005, z)
    rings = [R(C(z0 - 0.006), cx, cy), R(C(z0 - 0.006), cx + bw, cy + bw * 1.12, zfn=zf),
             R(C(z0 + 0.008), cx + bw, cy + bw * 1.12, zfn=zf), R(C(z0 + 0.010), cx, cy)]
    if h["round"]:
        rings += [R(C(z0 + ch * 0.55), cx * 0.98, cy * 0.98), R(C(z0 + ch * 0.85), cx * 0.80, cy * 0.80),
                  R(C(z0 + ch), cx * 0.40, cy * 0.40)]
    else:
        rings += [R(C(z0 + ch * 0.55), cx * 0.96, cy * 0.96), R(C(z0 + ch), cx * h["top"], cy * h["top"], rfn=pin),
                  R(C(z0 + ch - h["dent"] * 0.5), cx * h["top"] * 0.62, cy * h["top"] * 0.70),
                  R(C(z0 + ch - h["dent"]), cx * h["top"] * 0.25, cy * h["top"] * 0.45)]
    mb.loft(rings, h["color"], n=16, caps=(False, True))
    band(mb, [R(C(z0 + 0.012), cx + 0.006, cy + 0.006), R(C(z0 + 0.044), cx * 0.985 + 0.006, cy * 0.985 + 0.006)],
         h["band"], n=16)
    if h.get("buckle"):
        mb.box((cx + 0.006, -0.005, z0 + 0.028), (0.008, 0.030, 0.026), "brass")
    mb.xf = None
    mb.default_w = None


def build_neckwear(mb, c):
    col = c.get("bandana")
    if not col:
        return
    W = {"spine_03": 0.6, "neck_01": 0.4}
    band(mb, [R((0, 0, 1.495), 0.082, 0.080, w=W), R((0, -0.003, 1.560), 0.070, 0.068, w={"neck_01": 1})], col, n=10)
    g = c.get("girth", 1.0)
    mb.poly([(0.078, -0.060, 1.545), (-0.078, -0.060, 1.545), (0, -0.142 * g, 1.385), (0, -0.095, 1.555)],
            [(0, 2, 3), (3, 2, 1)], col, w={"spine_03": 1})
    mb.box((0.02, 0.088, 1.515), (0.05, 0.03, 0.035), col, w=W, rot=Euler((0, 0, radians(25))))


def build_gunbelt(mb, c):
    g = c.get("girth", 1.0)
    P = {"pelvis": 1}
    # trouser belt + buckle
    band(mb, [R((0, 0, 1.000), 0.194 * g, 0.134 * g, 0.136 * g, w=P), R((0, 0, 1.040), 0.186 * g, 0.130 * g, w=P)],
         "leather_dark", n=14, p=2.5)
    mb.box((0, -0.132 * g - 0.004, 1.020), (0.070, 0.016, 0.052), "brass", w=P)
    mb.box((0, -0.132 * g - 0.013, 1.020), (0.040, 0.006, 0.028), "steel_dark", w=P)
    # slung gun belt, low on the holster side
    sides = c.get("holsters", ("r",))
    tilt = 0.0 if len(sides) == 2 else radians(9)
    ax = Vector((sin(tilt), 0, cos(tilt)))
    ctr = Vector((0, 0, 0.955))
    band(mb, [R(ctr - ax * 0.024, 0.206 * g, 0.146 * g, w=P, axis=ax), R(ctr + ax * 0.024, 0.204 * g, 0.144 * g, w=P, axis=ax)],
         "leather", n=14, p=2.5)
    for k in range(7):  # cartridge loops across the back
        a = radians(150 + k * 10)
        p = ctr + Vector((0.206 * g * sin(a), -0.146 * g * cos(a) * 0.98, -sin(tilt) * 0.2 * sin(a)))
        mb.box(p, (0.012, 0.012, 0.040), "brass", w=P, rot=Euler((0, 0, -a)))
    for sd in sides:
        mb.mirror = sd == "r"
        th = "thigh_l"
        x = 0.216 * g
        mb.loft([R((x, -0.010, 0.985), 0.030, 0.056, w={"pelvis": 0.7, th: 0.3}),
                 R((x + 0.004, -0.014, 0.880), 0.030, 0.052, w={"pelvis": 0.3, th: 0.7}),
                 R((x + 0.002, -0.022, 0.760), 0.024, 0.036, w={th: 1}),
                 R((x, -0.026, 0.700), 0.018, 0.026, w={th: 1})], "leather", n=8, p=3.0)
        gl = 1 + (g - 1) * 0.6
        band(mb, [R((0.105 * gl, 0, 0.735), 0.100 * gl, 0.110 * gl, w={th: 1}), R((0.105 * gl, 0, 0.765), 0.102 * gl, 0.112 * gl, w={th: 1})],
             "leather_dark", n=10, p=2.3)
        if sd in c.get("holstered", ()):
            old = mb.xf
            s = props.GUN_SCALE
            rot = Matrix((Vector((1, 0, 0)), Vector((0, 0, 1)), Vector((0, -1, 0)))).transposed()
            mb.xf = Matrix.Translation((x + 0.004, 0.020, 0.965)) @ (rot * s).to_4x4()
            props.revolver(mb, w={"pelvis": 0.5, th: 0.5})
            mb.xf = old
    mb.mirror = False


def build_outerwear(mb, c):
    kind = c.get("outer")
    g = c.get("girth", 1.0)
    if kind == "vest":
        col = c["outer_color"]
        mb.loft(torso_rings([1.05, 1.12, 1.22, 1.34, 1.43, 1.487], k=1.0, g=g, add=0.014), col, n=14, arc=(16, 344),
                thick=0.014, p=2.5)
    elif kind == "duster":
        col = c["outer_color"]
        mb.loft(torso_rings([1.00, 1.12, 1.22, 1.34, 1.43, 1.49], g=g, add=0.018), col, n=14, arc=(15, 345),
                thick=0.016, p=2.5)

        def ww(zt):
            def f(a):
                sa, ca = sin(a), cos(a)
                side = abs(sa)
                leg = "thigh_l" if sa > 0 else "thigh_r"
                k = min(1.0, 0.35 + zt * 0.5) * (0.4 + 0.6 * side)
                return {"pelvis": 1 - k, leg: k}
            return f
        skirt = [(1.01, 0.192, 0.138, 0.0), (0.90, 0.215, 0.160, 0.3), (0.72, 0.245, 0.195, 0.7), (0.50, 0.275, 0.235, 1.0)]
        mb.loft([R((0, 0.01, z), rx * g, ry * g, w=ww(zt)) for z, rx, ry, zt in skirt], col, n=14, arc=(24, 336),
                thick=0.014, p=2.4)
        # collar
        mb.loft([R((0, 0, 1.475), 0.150, 0.105, w={"spine_03": 1}), R((0, 0.005, 1.555), 0.105, 0.095, w={"spine_03": 1})],
                col, n=12, arc=(40, 320), thick=0.012)
    elif kind == "poncho":
        col, trim = c["outer_color"], c.get("outer_trim", "cream")

        def pw(spine, k0):
            def f(a):
                sa = sin(a)
                k = k0 * abs(sa) ** 1.5
                return {spine: 1 - k, ("upperarm_l" if sa > 0 else "upperarm_r"): k}
            return f

        def hem(amp):
            return lambda a: -amp * cos(a) ** 2 + 0.04 * abs(sin(a)) ** 3

        def ring(z, rx, ry, spine, k0, amp, d=0.0):
            return R((0, 0, z), rx - d, ry - d, w=pw(spine, k0), zfn=hem(amp))
        spec = [(1.545, 0.088, 0.084, "spine_03", 0.0, 0.0), (1.475, 0.215, 0.140, "spine_03", 0.2, 0.0),
                (1.36, 0.300, 0.195, "spine_03", 0.6, 0.06), (1.22, 0.375, 0.240, "spine_02", 0.8, 0.17),
                (1.13, 0.405, 0.262, "spine_02", 0.8, 0.24), (1.07, 0.425, 0.275, "spine_02", 0.8, 0.28)]
        outer = [ring(*t) for t in spec]
        outer[3]["mat"] = trim
        inner = [ring(*t, 0.015) for t in reversed(spec[1:])]
        mb.loft(outer + inner, col, n=20, caps=(False, False), p=2.3)
        band(mb, [R((0, 0, 1.52), 0.10, 0.095, w={"spine_03": 1}), R((0, -0.004, 1.585), 0.082, 0.080, w={"neck_01": 1})],
             col, n=12)
    if c.get("bandolier"):
        ang = radians(38)
        ax = Vector((sin(ang), 0, cos(ang)))
        ctr = Vector((0, 0, 1.30))
        W = {"spine_02": 0.5, "spine_03": 0.5}
        band(mb, [R(ctr - ax * 0.028, 0.235 * g, 0.150 * g, w=W, axis=ax), R(ctr + ax * 0.028, 0.235 * g, 0.150 * g, w=W, axis=ax)],
             "leather", n=14, p=2.5)
        item = c["bandolier"]
        for k in range(5):
            t = (k - 2) * 0.052
            p = Vector((t * cos(ang) * 1.0, -0.150 * g - 0.010, 1.30 - t * sin(ang) * 1.0 / cos(ang) * cos(ang)))
            if item == "dynamite":
                mb.loft([R(p + ax * -0.05, 0.015, w=W), R(p + ax * 0.05, 0.015, w=W)], "dynamite", n=6)
            else:
                mb.box(p, (0.014, 0.014, 0.05), "brass", w=W, rot=Euler((0, ang, 0)))
    if c.get("badge"):
        ctr = Vector((0.095, -0.134 * g - 0.020, 1.365))
        pts = [ctr + Vector((0, -0.004, 0))]
        for k in range(10):
            r = 0.034 if k % 2 == 0 else 0.015
            a = 2 * pi * k / 10
            pts.append(ctr + Vector((r * sin(a), 0, r * cos(a))))
        mb.poly(pts, [(0, 1 + k, 1 + (k + 1) % 10) for k in range(10)], "brass", w={"spine_03": 1})


def hand_xf(up_canon_to=(0, -1, 0), origin=(0, 0.025, -0.040), scale=props.GUN_SCALE):
    """Canonical prop space -> left hand at rest. Barrel (-Y) runs along the arm, prop +Z faces the thumb side."""
    yg = -ARM_D
    zg = Vector(up_canon_to)
    xg = yg.cross(zg)
    rot = Matrix((xg, yg, zg)).transposed()
    return Matrix.Translation(GRIP) @ (rot * scale).to_4x4() @ Matrix.Translation(-Vector(origin))


def muzzle_tip(c):
    """Barrel tip in left-hand rest space; mirrored for the right hand."""
    wpn = c.get("weapon", "revolver")
    if wpn in ("rifle", "shotgun"):
        p, origin, scale = (0, -0.80 if wpn == "rifle" else -0.70, 0.034), (0, 0.035, -0.020), 1.12
    elif wpn == "dynamite":
        p, origin, scale = (0.03, -0.015, 0.17), (0, 0, -0.03), 1.2
    else:
        p, origin, scale = (0, -0.082 - (0.27 if c.get("long_barrel") else 0.21), 0.048), (0, 0.025, -0.040), props.GUN_SCALE
    return hand_xf(origin=origin, scale=scale) @ Vector(p)


def build_weapons(name, c, coll, arm, S):
    obs = []
    wpn = c.get("weapon", "revolver")
    sc = Matrix.Scale(S, 4)

    def make(suffix, fn, side="r", origin=(0, 0.025, -0.040), scale=props.GUN_SCALE, **kw):
        mb = MB(f"{name}_{suffix}")
        mb.xf = sc @ hand_xf(origin=origin, scale=scale)
        mb.mirror = side == "r"
        fn(mb, w={"weapon_l": 1}, **kw)
        obs.append(mb.finish(coll, arm))
    if wpn in ("revolver", "dual"):
        make("revolver_r", props.revolver, long_barrel=c.get("long_barrel", False))
        if wpn == "dual":
            make("revolver_l", props.revolver, side="l", long_barrel=c.get("long_barrel", False))
    elif wpn in ("rifle", "shotgun"):
        make(wpn, props.rifle, origin=(0, 0.035, -0.020), scale=1.12, shotgun=wpn == "shotgun")
    elif wpn == "dynamite":
        make("dynamite", props.dynamite, origin=(0, 0, -0.03), scale=1.2)
    return obs


def build_human(name, c, location=(0, 0, 0)):
    S = c.get("scale", 1.0)
    coll = get_collection(name, get_collection("WW_Characters"))
    arm = build_armature(name, skeleton(c), coll, scale=S)
    sc = Matrix.Scale(S, 4)
    parts = []
    for suffix, fn in (("body", build_body), ("hat", build_hat), ("neckwear", build_neckwear),
                       ("gunbelt", build_gunbelt), ("outerwear", build_outerwear)):
        if suffix == "hat" and c.get("hat") is False:
            continue
        mb = MB(f"{name}_{suffix}")
        mb.base = sc
        mb.xf = None
        _orig = mb.vert

        def vert(co, w=None, _o=_orig):
            v = _o(co, w)
            v.co = v.co * S
            return v
        mb.vert = vert
        fn(mb, c)
        if len(mb.bm.verts):
            parts.append(mb.finish(coll, arm))
        else:
            mb.bm.free()
    parts += build_weapons(name, c, coll, arm, S)
    arm.location = location
    arm["ww_kind"] = "human"
    return arm, parts
