"""Cowboy animation set. All actions are in place (no root motion) and authored at 30 fps.

Conventions: character faces -Y, left is +X. Arm and leg targets are in armature space, so planted feet stay
planted while the pelvis moves. `rot` values are degrees about the character axes; +X pitches an upright bone
forward and swings a hanging bone back.
"""
from math import cos, pi, radians, sin

from mathutils import Euler, Vector

from .core import Poser, aim, bell, ik, lerp, make_action, mirror_pose, rot, smooth

HX = 0.105          # hip / ankle half spacing
ANK_Z = 0.10


def merge(*ds):
    out = {}
    for d in ds:
        out.update(d)
    return out


def foot(side, pitch=0.0, yaw=10.0):
    """pitch > 0 = toes down. yaw > 0 = toes out."""
    sx = 1 if side == "l" else -1
    e = Euler((radians(pitch), 0, radians(-yaw * sx)), "XYZ").to_matrix()
    d = e @ Vector((0, -0.926, -0.376))
    up = e @ Vector((0, -0.376, 0.926))
    return {f"foot_{side}": aim(tuple(d), tuple(up))}


def leg(side, ank, pitch=0.0, yaw=10.0, knee_out=0.18, ball=0.0):
    sx = 1 if side == "l" else -1
    pole = (knee_out * sx, -1, 0.0)
    d = {f"thigh_{side}": ik(ank, pole, fwd=pole)}
    d.update(foot(side, pitch, yaw))
    if ball:
        d[f"ball_{side}"] = rot(x=-ball)
    return d


def stance(width=0.15, lf=0.0, rf=0.0, **kw):
    return merge(leg("l", (width, lf, ANK_Z), **kw), leg("r", (-width, rf, ANK_Z), **kw))


def fist(side, amt=1.0, thumb=1.0, trigger=None):
    sx = 1 if side == "l" else -1
    d = {}
    for f in ("index", "middle", "ring", "pinky"):
        a = amt if (trigger is None or f != "index") else trigger
        d[f"{f}_01_{side}"] = rot(y=75 * a * sx)
        d[f"{f}_02_{side}"] = rot(y=85 * a * sx)
    d[f"thumb_01_{side}"] = rot(y=35 * thumb * sx, x=25 * thumb)
    d[f"thumb_02_{side}"] = rot(y=30 * thumb * sx, x=30 * thumb)
    return d


def arm(side, wrist, pole, hand_dir, hand_fwd, grip=0.25, trigger=None):
    d = {f"upperarm_{side}": ik(wrist, pole), f"hand_{side}": aim(hand_dir, hand_fwd)}
    d.update(fist(side, grip, thumb=min(1.0, grip + 0.2), trigger=trigger))
    return d


def hang_l(sw=0.0, grip=0.3):
    return merge({"upperarm_l": aim((0.20, 0.05 + sw, -1)), "lowerarm_l": aim((0.12, -0.22 + sw * 1.2, -1)),
                  "hand_l": aim((0.05, -0.30 + sw, -1), (0.25, -1, 0))}, fist("l", grip, 0.3))


def hang_r(sw=0.0, grip=1.0):
    return merge({"upperarm_r": aim((-0.22, 0.06 + sw, -1)), "lowerarm_r": aim((-0.14, -0.28 + sw * 1.2, -1)),
                  "hand_r": aim((-0.10, -0.45 + sw, -1), (-0.2, -1, 0.35))}, fist("r", grip, 1.0, trigger=0.4))


def torso(lean=0.0, twist=0.0, side=0.0, head=(0, 0, 0), pelvis=(0, 0, 0), ploc=(0, 0, 0)):
    return {"pelvis": rot(pelvis[0], pelvis[1], pelvis[2], loc=ploc),
            "spine_01": rot(lean * 0.3, side * 0.3, twist * 0.3), "spine_02": rot(lean * 0.35, side * 0.35, twist * 0.35),
            "spine_03": rot(lean * 0.35, side * 0.35, twist * 0.35),
            "neck_01": rot(head[0] * 0.4, head[1] * 0.4, head[2] * 0.4), "head": rot(head[0] * 0.6, head[1] * 0.6, head[2] * 0.6)}


# ---------------------------------------------------------------- one-handed (revolver / dynamite)

def p_idle(b=0.0):
    return merge(torso(lean=2 + b * 1.5, twist=6, head=(-2 - b, 0, -6), ploc=(0.015, 0, -0.02 - 0.006 * b)),
                 stance(0.16, lf=-0.04, rf=0.05, yaw=16), hang_l(0.02 * b), hang_r(0.01 * b))


def p_aim(sway=0.0, recoil=0.0, target=(0, -1, 0.04)):
    t = Vector(target).normalized()
    w = Vector((-0.20, 0.0, 1.47)) + t * (0.50 - 0.10 * recoil) + Vector((0, 0, 0.012 * sway + 0.07 * recoil))
    hd = Vector((t.x, t.y, t.z + 0.40 * recoil))
    return merge(torso(lean=-2 - 5 * recoil, twist=24, head=(3, 0, -22), ploc=(0.0, 0.02 + 0.02 * recoil, -0.035)),
                 stance(0.17, lf=0.10, rf=-0.12, yaw=22),
                 arm("r", tuple(w), (-0.5, 0.2, -1), tuple(hd), (0, 0, 1) if recoil < 0.2 else (0, 0.5, 1), grip=1.0,
                     trigger=0.5 + 0.4 * recoil),
                 {"upperarm_l": aim((0.30, 0.18, -1)), "lowerarm_l": aim((0.10, -0.10, -1)),
                  "hand_l": aim((0.0, -0.2, -1), (0.4, -1, 0))}, fist("l", 0.5, 0.5))


def p_reload(k=0.0, open_=1.0):
    return merge(torso(lean=6, twist=8, head=(30, 0, -4), ploc=(0, 0, -0.03)), stance(0.16, lf=-0.03, rf=0.05, yaw=16),
                 arm("r", (-0.10, -0.32, 1.12), (-1, 0.3, -0.6), (0.35, -0.55, 0.75), (-0.3, 0.6, 0.75), grip=1.0),
                 arm("l", (0.06 + 0.03 * k, -0.36 - 0.02 * k, 1.17 + 0.05 * k), (1, 0.3, -0.7), (-0.8, -0.3, -0.2),
                     (0, -0.4, 1), grip=0.55 + 0.3 * k))


def p_hit(k=1.0):
    return merge(torso(lean=-16 * k, twist=6 - 22 * k, side=-8 * k, head=(-18 * k, 0, 10 * k),
                       ploc=(0.03 * k, 0.07 * k, -0.05 * k)),
                 stance(0.16, lf=-0.04, rf=0.05 + 0.12 * k, yaw=16),
                 {"upperarm_l": aim((0.5 * k + 0.2, 0.15, -1)), "lowerarm_l": aim((0.4 * k + 0.1, -0.4 * k - 0.2, -1 + 0.6 * k)),
                  "hand_l": aim((0.2, -0.4, -0.6), (0.25, -1, 0))},
                 fist("l", 0.1, 0.1), hang_r(0.10 * k))


def death_back():
    k0 = p_idle()
    k1 = p_hit(1.0)
    k2 = merge(torso(lean=-20, head=(-25, 0, 0), pelvis=(-32, 0, 0), ploc=(0, 0.26, -0.22)),
               leg("l", (0.17, -0.02, ANK_Z), yaw=20), leg("r", (-0.17, 0.30, ANK_Z), pitch=25, yaw=20),
               arm("l", (0.55, 0.20, 1.35), (0.5, 1, -0.5), (1, 0, 0.3), (0, -1, 0), grip=0.0),
               arm("r", (-0.55, 0.15, 1.20), (-0.5, 1, -0.5), (-1, -0.2, 0.2), (0, 0, 1), grip=0.8))
    k3 = merge(torso(lean=-8, head=(-10, 0, 8), pelvis=(-82, 0, 0), ploc=(0, 0.62, -0.82)),
               leg("l", (0.20, -0.18, 0.22), pitch=-20, yaw=25), leg("r", (-0.22, -0.05, 0.30), pitch=-10, yaw=25),
               arm("l", (0.60, 0.95, 0.30), (0.4, 0, 1), (0.8, 0.5, 0.1), (0, 0, 1), grip=0.0),
               arm("r", (-0.62, 0.85, 0.28), (-0.4, 0, 1), (-0.8, 0.5, 0.0), (0, 0, 1), grip=0.5))
    k4 = merge(torso(lean=0, head=(-4, 0, 14), pelvis=(-90, 0, 0), ploc=(0, 0.66, -0.86)),
               leg("l", (0.24, -0.22, 0.09), pitch=-35, yaw=35), leg("r", (-0.30, -0.10, 0.10), pitch=-30, yaw=40),
               arm("l", (0.66, 1.02, 0.06), (0.4, 0, 1), (0.8, 0.6, 0.0), (0, 0, 1), grip=0.1),
               arm("r", (-0.70, 0.80, 0.06), (-0.4, 0, 1), (-0.9, 0.3, 0.0), (0, 0, 1), grip=0.4))
    return [(0, k0), (5, k1), (14, k2), (24, k3), (27, merge(k4, torso(lean=3, head=(6, 0, 14), pelvis=(-86, 0, 0), ploc=(0, 0.66, -0.80)))),
            (32, k4), (45, k4)]


def death_forward():
    k0 = p_idle()
    k1 = merge(torso(lean=24, head=(20, 0, 0), ploc=(0, -0.04, -0.10)), stance(0.16, lf=-0.04, rf=0.05, yaw=16),
               arm("l", (0.12, -0.20, 1.12), (1, 0.4, -0.5), (-0.7, -0.3, -0.3), (0, -1, 0.4), grip=0.4),
               arm("r", (-0.10, -0.22, 1.08), (-1, 0.4, -0.5), (0.7, -0.3, -0.3), (0, -0.3, 1), grip=1.0))
    k2 = merge(torso(lean=20, head=(10, 0, 0), pelvis=(18, 0, 0), ploc=(0, -0.10, -0.50)),
               leg("l", (0.16, 0.05, ANK_Z), pitch=35, yaw=10, ball=35), leg("r", (-0.16, 0.22, ANK_Z), pitch=40, yaw=10, ball=35),
               arm("l", (0.20, -0.45, 0.60), (1, 0.4, 0.2), (0, -1, -0.4), (1, 0, 0), grip=0.1),
               arm("r", (-0.20, -0.42, 0.55), (-1, 0.4, 0.2), (0, -1, -0.4), (-1, 0, 0), grip=1.0))
    k3 = merge(torso(lean=4, head=(-12, 0, 30), pelvis=(86, 0, 0), ploc=(0, -0.45, -0.85)),
               leg("l", (0.18, 0.40, 0.07), pitch=70, yaw=20), leg("r", (-0.22, 0.45, 0.07), pitch=70, yaw=25),
               arm("l", (0.42, -1.00, 0.06), (1, 0, 0.6), (0.2, -1, 0), (1, 0, 0.2), grip=0.1),
               arm("r", (-0.36, -0.80, 0.06), (-1, 0, 0.6), (-0.3, -1, 0), (-1, 0, 0.2), grip=0.6))
    return [(0, k0), (8, k1), (18, k2), (28, k3), (31, merge(k3, torso(lean=6, head=(-16, 0, 30), pelvis=(84, 0, 0), ploc=(0, -0.45, -0.82)))),
            (35, k3), (45, k3)]


def gait(phase, stride, lift, stance_frac, run=False):
    """Ankle position and foot pitch for one leg at `phase` in [0,1). Contact happens at phase 0."""
    u = phase % 1.0
    if u < stance_frac:
        t = u / stance_frac
        y = lerp(-stride, stride, t)
        z = ANK_Z + (0.05 * smooth((t - 0.7) / 0.3) if t > 0.7 else 0.0)
        pitch = lerp(-18, 0, smooth(t / 0.25)) if t < 0.25 else lerp(0, 38, smooth((t - 0.6) / 0.4))
        ball = 30 * smooth((t - 0.6) / 0.4)
    else:
        t = (u - stance_frac) / (1 - stance_frac)
        y = lerp(stride, -stride, smooth(t))
        z = ANK_Z + 0.05 * (1 - t) + lift * bell(t * 0.9 + 0.05)
        pitch = lerp(38, -18, smooth(t * 1.2)) + (30 * bell(t) if run else 0)
        ball = 30 * (1 - smooth(t * 3))
    return y, z, pitch, ball


def p_walk(ph, run=False, armed="r"):
    stride, lift, sf = (0.30, 0.10, 0.60) if not run else (0.46, 0.26, 0.38)
    ly, lz, lp, lb = gait(ph, stride, lift, sf, run)
    ry, rz, rp, rb = gait(ph + 0.5, stride, lift, sf, run)
    c2, s1, c1 = cos(4 * pi * ph), sin(2 * pi * ph), cos(2 * pi * ph)
    bob = (-0.035 - 0.014 * c2) if not run else (-0.06 + 0.035 * sin(4 * pi * ph + 0.9))
    lean = 5 if not run else 16
    tw = 7 if not run else 12
    sw = 0.32 if not run else 0.55
    d = merge(torso(lean=lean, twist=-tw * c1, side=2 * s1, head=(-lean * 0.6, 0, tw * 0.6 * c1), pelvis=(0, -3 * s1, tw * 0.6 * c1),
                    ploc=(0.022 * s1, -0.02 if run else 0, bob)),
              leg("l", (HX + 0.02, ly, lz), pitch=lp, yaw=6, knee_out=0.08, ball=lb),
              leg("r", (-HX - 0.02, ry, rz), pitch=rp, yaw=6, knee_out=0.08, ball=rb))
    a = sw * c1  # left arm back when left foot is forward
    bend = 0.25 if not run else 1.0
    d.update(merge({"upperarm_l": aim((0.16, a, -1)), "lowerarm_l": aim((0.10, a * 1.2 - bend * 0.9, -1 + bend * 0.75)),
                    "hand_l": aim((0.05, a * 1.2 - bend - 0.1, -1 + bend * 0.8), (0.3, -1, -0.2))}, fist("l", 0.6 if run else 0.3, 0.4)))
    a = -sw * 0.6 * c1
    bend = 0.55 if not run else 1.0
    d.update(merge({"upperarm_r": aim((-0.18, a + 0.05, -1)), "lowerarm_r": aim((-0.10, a * 1.2 - bend * 0.9, -1 + bend * 0.7)),
                    "hand_r": aim((-0.05, a - bend - 0.2, -1 + bend * 0.9), (-0.2, -0.3, 1))}, fist("r", 1.0, 1.0, trigger=0.4)))
    return d


def p_throw(k):
    """k: 0 ready, 1 wound up, 2 release, 3 follow through."""
    table = {
        0: dict(tw=10, lean=0, w=(-0.30, -0.10, 1.20), hd=(0, -0.4, 1), lf=-0.05, rf=0.06, py=0.0),
        1: dict(tw=-42, lean=-10, w=(-0.42, 0.34, 1.62), hd=(0, 0.5, 1), lf=-0.16, rf=0.16, py=0.05),
        2: dict(tw=30, lean=10, w=(-0.20, -0.42, 1.80), hd=(0, -0.8, 0.8), lf=-0.18, rf=0.12, py=-0.04),
        3: dict(tw=48, lean=24, w=(0.10, -0.52, 1.05), hd=(0.2, -0.8, -0.6), lf=-0.18, rf=0.10, py=-0.08),
    }[k]
    lw = {0: (0.30, -0.10, 1.05), 1: (0.22, -0.48, 1.45), 2: (0.36, -0.10, 1.15), 3: (0.40, 0.20, 1.10)}[k]
    return merge(torso(lean=table["lean"], twist=table["tw"], head=(0, 0, -table["tw"] * 0.7), ploc=(0, table["py"], -0.05)),
                 stance(0.17, lf=table["lf"], rf=table["rf"], yaw=20),
                 arm("r", table["w"], (-1, 0.5, -0.4), table["hd"], (0, -1, 0.3), grip=1.0 if k < 2 else 0.1),
                 arm("l", lw, (1, 0.3, -0.8), (0.2, -1, 0.2), (0, 0, 1), grip=0.2))


def p_cover(b=0.0):
    return merge(torso(lean=26 + b, twist=10, head=(-14, 0, -8), ploc=(0.0, 0.10, -0.50 - 0.005 * b)),
                 leg("l", (0.17, -0.16, ANK_Z), yaw=14), leg("r", (-0.16, 0.34, 0.13), pitch=55, yaw=6, ball=45),
                 arm("r", (-0.24, -0.20, 0.98), (-1, 0.4, -0.3), (0.1, -0.35, 1), (0.3, 1, 0.4), grip=1.0, trigger=0.3),
                 arm("l", (0.26, -0.34, 0.62), (1, 0.2, 0.3), (0, -0.6, -1), (0.4, -1, 0), grip=0.2))


def p_climb(ph):
    s, c = sin(2 * pi * ph), cos(2 * pi * ph)
    up_l, up_r = 0.5 + 0.5 * c, 0.5 - 0.5 * c
    return merge(torso(lean=6, twist=-8 * c, side=5 * c, head=(-24, 0, 0), ploc=(0.03 * c, -0.10, -0.10 + 0.04 * abs(s))),
                 leg("l", (0.15, -0.26, 0.12 + 0.42 * up_r + 0.05 * bell(ph * 2 % 1)), pitch=20, knee_out=0.5),
                 leg("r", (-0.15, -0.26, 0.12 + 0.42 * up_l), pitch=20, knee_out=0.5),
                 arm("l", (0.20, -0.42, 1.52 + 0.44 * up_l), (1, 0.3, -0.6), (0, -0.3, 1), (-1, 0, 0), grip=0.9),
                 arm("r", (-0.20, -0.42, 1.52 + 0.44 * up_r), (-1, 0.3, -0.6), (0, -0.3, 1), (1, 0, 0), grip=0.9))


def p_showdown(b=0.0, tw=0.0):
    return merge(torso(lean=4, twist=0, head=(6, 0, 0), ploc=(0, 0, -0.06)), stance(0.24, lf=0.0, rf=0.02, yaw=24),
                 {"upperarm_r": aim((-0.55, 0.12, -1)), "lowerarm_r": aim((-0.10, -0.25, -1)),
                  "hand_r": aim((0.05 + 0.04 * tw, -0.35, -1), (-0.3, -1, 0.3))}, fist("r", 0.9 - 0.15 * tw, 0.8, trigger=0.2),
                 {"upperarm_l": aim((0.55, 0.10, -1)), "lowerarm_l": aim((0.12, -0.25, -1)),
                  "hand_l": aim((0.0, -0.3, -1), (0.3, -1, 0))}, fist("l", 0.35 + 0.1 * b, 0.3))


# ---------------------------------------------------------------- two-handed (rifle / shotgun)

def p_long(w, b, up=(0, 0, 1), lean=0, twist=30, head=(4, 8, -28), support=0.46, lf=0.10, rf=-0.14, ploc=(0, 0.02, -0.04),
           recoil=0.0, lever=0.0):
    w, b, up = Vector(w), Vector(b).normalized(), Vector(up)
    w = w - b * 0.06 * recoil
    side = b.cross(up).normalized()
    lw = w + b * support - up * 0.035 - side * 0.035
    return merge(torso(lean=lean - 6 * recoil, twist=twist, head=head, ploc=ploc), stance(0.18, lf=lf, rf=rf, yaw=24),
                 arm("r", tuple(w + up * 0.02 * recoil - up * 0.05 * lever), (-1, 0.5, -0.6), tuple(b + up * (0.18 * recoil - 0.3 * lever)), tuple(up), grip=1.0 - 0.4 * lever, trigger=0.5),
                 arm("l", tuple(lw), (1, 0.2, -1), tuple(b * 0.6 - side * 0.7 + up * 0.35), tuple(b * 0.8 + side * 0.5), grip=0.75))


def p_rifle_idle(b=0.0):
    return p_long((-0.20, -0.16, 1.06), (0.62, -0.62, 0.48), up=(0.2, 0.5, 1), twist=8, head=(-2, 0, -8), support=0.44, lf=-0.04,
                  rf=0.05, ploc=(0, 0, -0.02 - 0.005 * b), lean=2 + b)


def p_rifle_aim(recoil=0.0, lever=0.0, sway=0.0):
    return p_long((-0.155, -0.20, 1.475 + 0.006 * sway), (0.02, -1, 0.02), twist=34, head=(6, 10, -30), recoil=recoil, lever=lever)


# ---------------------------------------------------------------- mounted

def p_ride(ph=0.0, amp=1.0, mode="reins", recoil=0.0):
    """ph = gallop phase. The rider's root is pinned to the horse's rider socket."""
    s, c = sin(2 * pi * ph), cos(2 * pi * ph)
    s2 = sin(2 * pi * ph - 1.0)
    bounce = 0.045 * amp * s
    lean = 14 + 9 * amp * c
    legs = merge(leg("l", (0.37, -0.20 - 0.03 * amp * c, 0.37), pitch=-12, yaw=24, knee_out=1.1),
                 leg("r", (-0.37, -0.20 - 0.03 * amp * c, 0.37), pitch=-12, yaw=24, knee_out=1.1))
    rein_y = -0.40 - 0.05 * amp * s2
    rein_z = 1.12 + 0.03 * amp * c
    left = arm("l", (0.07, rein_y, rein_z), (1, 0.3, -0.8), (-0.35, -1, 0.1), (0, 0, 1), grip=1.0)
    tw, head = 0, (-lean * 0.7, 0, 0)
    if mode == "reins":
        right = arm("r", (-0.09, rein_y + 0.06, rein_z - 0.10), (-1, 0.3, -0.8), (0.2, -0.8, -0.6), (-0.2, 0, 1), grip=1.0, trigger=0.4)
    else:
        sx = -1 if mode == "aim_r" else 1
        t = Vector((sx * 1.0, -0.22, 0.03)).normalized()
        tw = -62 * sx if sx > 0 else 38
        sh = Vector((-0.05 if sx > 0 else -0.24, -0.06, 1.47))
        w = sh + t * (0.50 - 0.10 * recoil) + Vector((0, 0, 0.07 * recoil - 0.5 * bounce))
        right = arm("r", tuple(w), (0, 0.4, -1), (t.x, t.y, t.z + 0.55 * recoil), (0, 0, 1), grip=1.0, trigger=0.5 + 0.4 * recoil)
        head = (-lean * 0.5, 0, -72 * sx * 0.9 - tw * 0.0)
        head = (0, 0, (-80 * sx) - tw)
        lean *= 0.6
    return merge(torso(lean=lean, twist=tw, head=head, pelvis=(-4 * amp * c, 0, 0), ploc=(0, 0.02 * amp * c, bounce)), legs, left, right)


def ride_death():
    k0 = p_ride(0.0, 0.6)
    k1 = merge(p_ride(0.2, 0.6), torso(lean=-22, twist=-20, side=-10, head=(-25, 0, 10), ploc=(0.03, 0.06, 0.04)),
               arm("l", (0.55, 0.10, 1.55), (1, 1, -0.5), (1, 0, 0.4), (0, -1, 0), grip=0.0),
               arm("r", (-0.50, 0.20, 1.35), (-1, 1, -0.5), (-1, 0, 0.2), (0, 0, 1), grip=0.7))
    k2 = merge(torso(lean=-30, side=-38, head=(-20, 0, 0), pelvis=(-20, -40, 0), ploc=(0.30, 0.20, -0.05)),
               leg("l", (0.62, -0.10, 0.62), pitch=10, knee_out=1.0), leg("r", (-0.10, -0.05, 0.62), pitch=10, knee_out=-0.4),
               arm("l", (0.95, 0.30, 1.20), (0, 1, 0.5), (1, 0.2, -0.3), (0, -1, 0), grip=0.0),
               arm("r", (-0.25, 0.40, 1.70), (-1, 1, 0), (-0.4, 0.4, 1), (0, -1, 0), grip=0.5))
    k3 = merge(torso(lean=-10, side=-20, head=(-10, 0, 20), pelvis=(-30, -105, 0), ploc=(0.85, 0.45, -0.75)),
               leg("l", (1.30, 0.20, -0.30), pitch=30, knee_out=1.0), leg("r", (0.75, -0.30, 0.55), pitch=30, knee_out=-0.4),
               arm("l", (1.20, 0.95, -0.35), (0, 1, 1), (1, 0.6, -0.4), (0, -1, 0), grip=0.0),
               arm("r", (0.30, 0.95, 0.45), (0, 1, 1), (0, 1, 0.2), (0, 0, 1), grip=0.3))
    return [(0, k0), (5, k1), (13, k2), (22, k3)]


# ---------------------------------------------------------------- gatling crew

def p_gatling(ph):
    s, c = sin(2 * pi * ph), cos(2 * pi * ph)
    shake = 0.006 * sin(8 * pi * ph)
    return merge(torso(lean=14, twist=-6, head=(-6, 0, 4), ploc=(0, shake, -0.10)), stance(0.24, lf=-0.10, rf=0.14, yaw=20),
                 arm("r", (-0.06, -0.40 + shake, 1.01), (-1, 0.5, -0.5), (0.15, -1, 0.1), (0.4, 0, 1), grip=1.0),
                 arm("l", (0.215, -0.62 + 0.082 * c + shake, 1.045 + 0.082 * s), (1, 0.4, -0.4), (-0.3, -1, 0), (0, 0, 1), grip=1.0))


# ---------------------------------------------------------------- build

def build_human_actions(arm_ob):
    P = Poser(arm_ob)
    A = {}

    def add(name, keys, loop=False):
        A[name] = make_action(arm_ob, P, name, keys, loop)

    add("idle", [(0, p_idle(0)), (30, p_idle(1)), (60, p_idle(0))], loop=True)
    add("walk", [(f, p_walk(f / 32)) for f in range(0, 33, 2)], loop=True)
    add("run", [(f, p_walk(f / 20, run=True)) for f in range(0, 21)], loop=True)
    add("aim_start", [(0, p_idle(0)), (7, merge(p_idle(0), arm("r", (-0.34, 0.06, 1.05), (-1, 0.6, -0.2), (-0.1, -0.7, -0.7), (-0.2, -0.4, 1), grip=1.0))),
                      (16, p_aim(0.5, 0.12)), (21, p_aim(0))])
    add("aim", [(0, p_aim(0)), (20, p_aim(1)), (40, p_aim(0))], loop=True)
    add("shoot", [(0, p_aim(0)), (2, p_aim(0, 1.0)), (5, p_aim(0, 0.55)), (10, p_aim(0, 0.08)), (14, p_aim(0))])
    add("reload", [(0, p_aim(0)), (8, p_reload(0)), (14, p_reload(1)), (19, p_reload(0)), (24, p_reload(1)), (29, p_reload(0)),
                   (34, p_reload(1)), (40, p_reload(0)), (50, p_aim(0))])
    add("hit", [(0, p_idle(0)), (3, p_hit(1.0)), (8, p_hit(0.5)), (16, p_idle(0))])
    add("hit_aim", [(0, p_aim(0)), (3, p_hit(1.0)), (8, p_hit(0.4)), (16, p_aim(0))])
    add("death_back", death_back())
    add("death_forward", death_forward())
    add("throw", [(0, p_idle(0)), (6, p_throw(0)), (16, p_throw(1)), (20, p_throw(1)), (24, p_throw(2)), (29, p_throw(3)),
                  (42, p_idle(0))])
    add("cover_idle", [(0, p_cover(0)), (20, p_cover(2)), (40, p_cover(0))], loop=True)
    add("cover_popup", [(0, p_cover(0)), (5, merge(p_aim(0), torso(lean=12, twist=20, head=(0, 0, -18), ploc=(0, 0.05, -0.22)))), (10, p_aim(0))])
    add("cover_hide", [(0, p_aim(0)), (5, merge(p_aim(0), torso(lean=16, twist=16, head=(0, 0, -14), ploc=(0, 0.06, -0.28)))), (11, p_cover(0))])
    add("climb", [(f, p_climb(f / 28)) for f in range(0, 29, 2)], loop=True)
    add("showdown_idle", [(0, p_showdown(0, 0)), (12, p_showdown(1, 1)), (24, p_showdown(0, 0)), (34, p_showdown(1, 1)), (48, p_showdown(0, 0))], loop=True)
    add("quickdraw", [(0, p_showdown(0, 0)), (3, merge(p_showdown(0, 0), arm("r", (-0.36, 0.04, 1.02), (-1, 0.6, -0.2), (-0.1, -0.8, -0.5), (-0.2, -0.4, 1), grip=1.0))),
                      (7, p_aim(0, 0.1)), (9, p_aim(0, 1.0)), (13, p_aim(0, 0.4)), (20, p_aim(0))])
    # long guns
    add("rifle_idle", [(0, p_rifle_idle(0)), (30, p_rifle_idle(1)), (60, p_rifle_idle(0))], loop=True)
    add("rifle_aim_start", [(0, p_rifle_idle(0)), (14, p_rifle_aim(sway=1)), (20, p_rifle_aim())])
    add("rifle_aim", [(0, p_rifle_aim()), (20, p_rifle_aim(sway=1)), (40, p_rifle_aim())], loop=True)
    add("rifle_shoot", [(0, p_rifle_aim()), (2, p_rifle_aim(1.0)), (6, p_rifle_aim(0.4)), (10, p_rifle_aim(0.1, 0.0)), (15, p_rifle_aim(0, 1.0)),
                        (20, p_rifle_aim(0, 0.0)), (24, p_rifle_aim())])
    # mounted
    add("ride_idle", [(0, p_ride(0, 0.12)), (20, p_ride(0.5, 0.12)), (40, p_ride(1.0, 0.12))], loop=True)
    for nm, mode in (("ride_gallop", "reins"), ("ride_aim_left", "aim_l"), ("ride_aim_right", "aim_r")):
        add(nm, [(f, p_ride(f / 14, 1.0, mode)) for f in range(0, 15)], loop=True)
    for nm, mode in (("ride_shoot_left", "aim_l"), ("ride_shoot_right", "aim_r")):
        rc = [0, 0.5, 1.0, 0.8, 0.55, 0.35, 0.2, 0.1, 0.05, 0, 0, 0, 0, 0, 0]
        add(nm, [(f, p_ride(f / 14, 1.0, mode, recoil=rc[f])) for f in range(0, 15)])
    add("ride_death", ride_death())
    add("gatling_fire", [(f, p_gatling(f / 12)) for f in range(0, 13)], loop=True)
    return A
