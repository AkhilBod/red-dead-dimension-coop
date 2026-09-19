"""Bullets, tracers, flashes, puffs and the player's first-person revolver."""
import random
from math import cos, pi, radians, sin

from mathutils import Euler, Vector

from .core import MB, R, Poser, build_armature, cyl, get_collection, make_action, rot


def bullet(mb):
    """Cartridge along -Y, 10x life size so it reads in a HUD or slow-motion shot. Scale down in engine."""
    cyl(mb, (0, 0.20, 0), (0, 0.17, 0), 0.062, "brass", n=10, front=(0, 0, 1))
    cyl(mb, (0, 0.17, 0), (0, -0.10, 0), 0.055, "brass", n=10, front=(0, 0, 1))
    mb.loft([R((0, -0.10, 0), 0.053), R((0, -0.20, 0), 0.045), R((0, -0.26, 0), 0.022), R((0, -0.275, 0), 0.006)], "lead", n=10, front=(0, 0, 1))


def casing(mb):
    cyl(mb, (0, 0.20, 0), (0, 0.17, 0), 0.062, "brass", n=10, front=(0, 0, 1))
    cyl(mb, (0, 0.17, 0), (0, -0.10, 0), 0.055, "brass", n=10, front=(0, 0, 1), caps=(True, False))
    cyl(mb, (0, -0.10, 0), (0, 0.12, 0), 0.047, "iron", n=10, front=(0, 0, 1), caps=(False, True))


def slug(mb):
    mb.loft([R((0, 0.05, 0), 0.05), R((0, -0.04, 0), 0.05), R((0, -0.10, 0), 0.025), R((0, -0.115, 0), 0.006)], "lead", n=8, front=(0, 0, 1))


def tracer(mb, length=1.6, r=0.035, mat="tracer"):
    """Stretched diamond along -Y, origin at the tail. Enemy shots use the slow fat one."""
    mb.loft([R((0, 0, 0), 0.002), R((0, -length * 0.75, 0), r), R((0, -length, 0), 0.002)], mat, n=4, p=2, front=(0, 0, 1))


def muzzle_flash(mb, s=1.0, seed=1):
    """Star burst pointing -Y, origin at the muzzle."""
    rng = random.Random(seed)
    mb.loft([R((0, 0, 0), 0.02 * s), R((0, -0.10 * s, 0), 0.10 * s), R((0, -0.55 * s, 0), 0.004)], "tracer", n=6, front=(0, 0, 1))
    for k in range(6):
        a = 2 * pi * k / 6 + 0.3
        d = Vector((cos(a), -0.35, sin(a))).normalized()
        L = s * rng.uniform(0.22, 0.38)
        mb.loft([R((0, -0.03 * s, 0), 0.035 * s), R(d * L + Vector((0, -0.03 * s, 0)), 0.003)], "flash", n=4, front=(0, -1, 0.2))


def puff(mb, seed=1, r=0.5, mat="sand_light"):
    """Faceted blob for mesh particles: dust, smoke, steam."""
    rng = random.Random(seed)

    def j(a):
        return 1.0 + rng.uniform(-0.18, 0.18)
    mb.loft([R((0, 0, -r), 0.05), R((0, 0, -r * 0.6), r * 0.75, rfn=j), R((0, 0, 0), r, rfn=j), R((0, 0, r * 0.6), r * 0.78, rfn=j), R((0, 0, r), 0.05)], mat, n=7)


def shard(mb, mat="bottle_green"):
    mb.poly([(0, 0, 0), (0.05, 0, 0.01), (0.015, 0, 0.07), (0.02, 0.008, 0.02)], [(0, 1, 2), (0, 3, 1), (1, 3, 2), (2, 3, 0)], mat)


def splinter(mb):
    mb.box((0, 0, 0), (0.03, 0.22, 0.012), "plank_light", taper=(0.2, 1.0))


# ---------------------------------------------------------------- first-person revolver

def player_revolver():
    """Rig: root > gun > cylinder / hammer / trigger. Barrel along -Y, origin at the grip. Camera sits ~0.35 behind, 0.12 above."""
    cyl_c = Vector((0, -0.045, 0.045))
    ham_p = Vector((0, 0.030, 0.062))
    tri_p = Vector((0, -0.030, 0.012))
    G, C, H, T = {"gun": 1}, {"cylinder": 1}, {"hammer": 1}, {"trigger": 1}

    def fn(mb):
        # grip
        mb.loft([R((0, 0.052, -0.115), 0.017, 0.030, w=G), R((0, 0.040, -0.060), 0.018, 0.026, w=G), R((0, 0.016, -0.012), 0.018, 0.026, w=G),
                 R((0, 0.006, 0.022), 0.015, 0.022, w=G)], "wood", n=8, p=2.6)
        mb.box((0, 0.056, -0.121), (0.040, 0.066, 0.010), "brass", w=G)
        for sx in (1, -1):
            mb.box((sx * 0.019, 0.034, -0.050), (0.004, 0.030, 0.060), "bone", w=G)
        # frame, top strap, recoil shield
        mb.box((0, -0.040, 0.004), (0.030, 0.150, 0.030), "steel_dark", w=G)
        mb.box((0, -0.045, 0.086), (0.018, 0.130, 0.012), "steel_dark", w=G)
        mb.box((0, 0.014, 0.045), (0.034, 0.020, 0.080), "steel_dark", w=G)
        mb.box((0, -0.103, 0.045), (0.030, 0.020, 0.080), "steel_dark", w=G)
        # cylinder with flutes
        mb.loft([R(cyl_c + Vector((0, 0.044, 0)), 0.030, w=C), R(cyl_c + Vector((0, 0.038, 0)), 0.0335, w=C), R(cyl_c + Vector((0, -0.040, 0)), 0.0335, w=C),
                 R(cyl_c + Vector((0, -0.046, 0)), 0.030, w=C)], "steel", n=12, front=(0, 0, 1))
        for k in range(6):
            a = 2 * pi * k / 6
            o = Vector((sin(a) * 0.031, 0, cos(a) * 0.031))
            mb.box(cyl_c + o + Vector((0, -0.018, 0)), (0.012, 0.046, 0.008), "steel_dark", w=C, rot=Euler((0, a, 0)))
            o2 = Vector((sin(a + pi / 6) * 0.020, 0.0445, cos(a + pi / 6) * 0.020))
            cyl(mb, cyl_c + o2, cyl_c + o2 + Vector((0, 0.003, 0)), 0.0075, "brass", n=6, w=C, front=(0, 0, 1))
        # barrel, ejector, sight
        mb.loft([R((0, -0.113, 0.062), 0.0150, w=G), R((0, -0.330, 0.062), 0.0135, w=G)], "steel", n=8, front=(0, 0, 1))
        cyl(mb, (0, -0.331, 0.062), (0, -0.325, 0.062), 0.0075, "black", n=6, w=G, front=(0, 0, 1))
        cyl(mb, (0.012, -0.115, 0.036), (0.012, -0.250, 0.036), 0.0075, "steel_dark", n=6, w=G, front=(0, 0, 1))
        mb.box((0, -0.318, 0.083), (0.005, 0.018, 0.016), "steel_dark", w=G, taper=(1, 0.3))
        # hammer, trigger, guard
        mb.box(ham_p + Vector((0, 0.010, 0.022)), (0.010, 0.020, 0.044), "steel_dark", w=H, rot=Euler((radians(-20), 0, 0)))
        mb.box(ham_p + Vector((0, 0.026, 0.044)), (0.014, 0.022, 0.008), "steel_dark", w=H, rot=Euler((radians(-35), 0, 0)))
        mb.box(tri_p + Vector((0, 0.002, -0.018)), (0.006, 0.008, 0.030), "steel", w=T, rot=Euler((radians(18), 0, 0)))
        mb.box((0, -0.034, -0.040), (0.008, 0.070, 0.006), "brass", w=G)
        mb.box((0, -0.070, -0.022), (0.008, 0.006, 0.040), "brass", w=G)
        mb.box((0, 0.000, -0.024), (0.008, 0.006, 0.036), "brass", w=G)
        # hand and sleeve
        mb.loft([R((0.004, 0.030, -0.115), 0.040, 0.046, w=G), R((0.006, 0.040, -0.050), 0.044, 0.048, w=G), R((0.006, 0.050, 0.005), 0.040, 0.044, w=G)],
                "leather_dark", n=8, p=3.0)
        for k, z in enumerate((-0.028, -0.056, -0.084, -0.108)):
            mb.box((-0.006, -0.012 + k * 0.004, z), (0.060, 0.050, 0.024), "leather_dark", w=G)
        mb.box((-0.030, -0.052, 0.014), (0.016, 0.060, 0.018), "leather_dark", w=T)          # trigger finger
        mb.box((0.030, -0.010, 0.030), (0.020, 0.070, 0.022), "leather_dark", w=G, rot=Euler((0, 0, radians(12))))   # thumb
        mb.loft([R((0.006, 0.070, -0.050), 0.042, 0.044, w=G), R((0.030, 0.200, -0.110), 0.050, w=G)], "leather_dark", n=8)
        mb.loft([R((0.028, 0.185, -0.104), 0.060, w=G), R((0.040, 0.235, -0.128), 0.058, w=G), R((0.120, 0.560, -0.300), 0.072, w=G)], "charcoal", n=8)

    coll = get_collection("PlayerRevolver", get_collection("WW_FX"))
    mb = MB("PlayerRevolver_mesh")
    fn(mb)
    bones = [("root", None, (0, 0, 0), (0, 0.1, 0)), ("gun", "root", (0, 0.03, -0.05), (0, -0.07, -0.05)),
             ("cylinder", "gun", tuple(cyl_c + Vector((0, 0.04, 0))), tuple(cyl_c + Vector((0, -0.04, 0)))),
             ("hammer", "gun", tuple(ham_p), tuple(ham_p + Vector((0, 0, 0.05)))), ("trigger", "gun", tuple(tri_p), tuple(tri_p + Vector((0, 0, -0.03)))),
             ("muzzle", "gun", (0, -0.335, 0.062), (0, -0.40, 0.062))]
    arm = build_armature("PlayerRevolver", bones, coll)
    ob = mb.finish(coll, arm)
    P = Poser(arm)

    def pose(kick=0.0, back=0.0, ham=0.0, tri=0.0, cyl_a=0.0, gx=0.0, gz=0.0, roll=0.0, pitch=0.0):
        return {"gun": rot(x=-kick + pitch, y=roll, loc=(gx, back, gz + 0.00001)), "cylinder": rot(y=cyl_a), "hammer": rot(x=-ham), "trigger": rot(x=-tri)}
    make_action(arm, P, "fp_idle", [(0, pose()), (30, pose(gz=0.004, pitch=0.6)), (60, pose())], loop=True)
    make_action(arm, P, "fp_fire", [(0, pose(ham=38, tri=14)), (1, pose(kick=20, back=0.05, gz=0.02, tri=14)), (3, pose(kick=28, back=0.06, gz=0.035, cyl_a=20)),
                                    (7, pose(kick=10, back=0.02, gz=0.01, cyl_a=50, ham=20)), (12, pose(cyl_a=60, ham=38)), (16, pose(cyl_a=60))])
    make_action(arm, P, "fp_dry_fire", [(0, pose(ham=38, tri=14)), (1, pose(tri=14, kick=1.5)), (5, pose())])
    rl = [(0, pose()), (8, pose(roll=70, pitch=-32, gx=-0.06, gz=0.05)), (12, pose(roll=70, pitch=-32, gx=-0.06, gz=0.05, cyl_a=90))]
    for k in range(6):
        rl.append((14 + k * 3, pose(roll=70, pitch=-32, gx=-0.06, gz=0.05 - 0.006 * (k % 2), cyl_a=90 + 60 * (k + 1))))
    rl += [(36, pose(roll=40, pitch=-16, gx=-0.03, gz=0.03, cyl_a=90 + 360)), (42, pose(kick=4, cyl_a=90 + 360 + 30)), (46, pose(cyl_a=120))]
    for i, (f, p) in enumerate(rl):
        p["cylinder"] = rot(y=p["cylinder"]["rel"][1] % 360)
    make_action(arm, P, "fp_reload", rl)
    make_action(arm, P, "fp_draw", [(0, pose(pitch=-75, gz=-0.45, back=0.10)), (5, pose(pitch=-20, gz=-0.10)), (8, pose(pitch=4, gz=0.01, ham=38)), (11, pose(ham=38))])
    make_action(arm, P, "fp_holster", [(0, pose()), (4, pose(pitch=8)), (12, pose(pitch=-75, gz=-0.45, back=0.10))])
    make_action(arm, P, "fp_spin", [(f, pose(pitch=-(360 * f / 16) % 360 if f < 16 else 0)) for f in range(0, 17, 2)])
    from .core import set_action
    import bpy
    set_action(arm, bpy.data.actions["fp_idle"])
    arm["ww_kind"] = "fp"
    return arm, ob


FX = {
    "Bullet": (bullet, {}), "Casing": (casing, {}), "Slug": (slug, {}), "Tracer_Player": (tracer, dict(length=2.2, r=0.025)),
    "Tracer_Enemy": (tracer, dict(length=1.1, r=0.07, mat="flash")), "MuzzleFlash_A": (muzzle_flash, dict(seed=1)),
    "MuzzleFlash_B": (muzzle_flash, dict(seed=4, s=1.3)), "MuzzleFlash_Big": (muzzle_flash, dict(seed=7, s=2.2)),
    "Puff_Dust": (puff, dict(seed=1)), "Puff_Smoke": (puff, dict(seed=2, r=0.7, mat="charcoal")), "Puff_Steam": (puff, dict(seed=3, r=0.8, mat="paint_white")),
    "Shard_Glass": (shard, {}), "Splinter": (splinter, {}),
}
