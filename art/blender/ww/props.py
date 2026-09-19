"""Weapons and props. Canonical space: barrel along -Y, up +Z, origin where the firing hand grips."""
from math import cos, pi, radians, sin

from mathutils import Euler, Vector

from .core import MB, R, get_collection

GUN_SCALE = 1.25


def revolver(mb, w=None, long_barrel=False):
    mb.default_w = w
    bl = 0.27 if long_barrel else 0.21
    # grip
    mb.loft([R((0, 0.040, -0.092), 0.014, 0.024), R((0, 0.030, -0.050), 0.015, 0.021),
             R((0, 0.012, -0.010), 0.015, 0.022), R((0, 0.004, 0.018), 0.013, 0.020)],
            "wood", n=6, p=2.6, front=(0, -1, 0))
    mb.box((0, 0.044, -0.097), (0.034, 0.054, 0.010), "brass")
    # frame
    mb.box((0, -0.030, 0.034), (0.026, 0.115, 0.044), "steel_dark", taper=(0.85, 1.0))
    mb.box((0, -0.035, 0.062), (0.016, 0.105, 0.012), "steel_dark")
    # cylinder
    mb.loft([R((0, -0.004, 0.034), 0.0255), R((0, -0.012, 0.034), 0.0275), R((0, -0.056, 0.034), 0.0275),
             R((0, -0.064, 0.034), 0.0255)], "steel", n=8, front=(0, 0, 1))
    # barrel + ejector
    mb.loft([R((0, -0.082, 0.048), 0.0125), R((0, -0.082 - bl, 0.048), 0.0115)], "steel", n=6, front=(0, 0, 1))
    mb.box((0, -0.082 - bl * 0.45, 0.030), (0.010, bl * 0.8, 0.010), "steel_dark")
    mb.box((0, -0.076 - bl, 0.064), (0.005, 0.014, 0.012), "steel_dark")
    # hammer, trigger guard
    mb.box((0, 0.028, 0.068), (0.009, 0.026, 0.022), "steel_dark", rot=Euler((radians(-30), 0, 0)))
    mb.box((0, -0.030, -0.014), (0.007, 0.050, 0.006), "steel_dark")
    mb.box((0, -0.056, 0.000), (0.007, 0.006, 0.030), "steel_dark")
    mb.box((0, -0.026, 0.002), (0.005, 0.006, 0.022), "steel", rot=Euler((radians(20), 0, 0)))
    mb.default_w = None


def rifle(mb, w=None, shotgun=False):
    mb.default_w = w
    # stock
    mb.loft([R((0, 0.400, -0.060), 0.016, 0.058), R((0, 0.385, -0.058), 0.020, 0.062),
             R((0, 0.200, -0.030), 0.019, 0.042), R((0, 0.070, -0.004), 0.018, 0.028)],
            "wood", n=6, p=3.0, front=(0, 0, 1))
    mb.box((0, 0.408, -0.060), (0.036, 0.012, 0.120), "leather_dark")
    # receiver
    mb.box((0, -0.030, 0.012), (0.036, 0.200, 0.062), "steel_dark" if not shotgun else "steel", taper=(0.8, 1.0))
    mb.box((0, 0.055, 0.048), (0.010, 0.030, 0.020), "steel", rot=Euler((radians(-30), 0, 0)))
    # lever / trigger guard
    if shotgun:
        mb.box((0, 0.000, -0.030), (0.008, 0.070, 0.006), "steel_dark")
        mb.box((0, -0.034, -0.022), (0.008, 0.006, 0.020), "steel_dark")
    else:
        mb.box((0, 0.020, -0.046), (0.009, 0.120, 0.008), "brass")
        mb.box((0, 0.078, -0.028), (0.009, 0.008, 0.040), "brass")
        mb.box((0, -0.038, -0.030), (0.009, 0.008, 0.036), "brass")
    mb.box((0, -0.010, -0.016), (0.005, 0.006, 0.022), "steel", rot=Euler((radians(20), 0, 0)))
    # forestock
    mb.loft([R((0, -0.130, 0.004), 0.021, 0.026), R((0, -0.300, 0.006), 0.020, 0.023),
             R((0, -0.440, 0.010), 0.017, 0.019)], "wood_dark", n=6, p=3.0, front=(0, 0, 1))
    if shotgun:
        for x in (-0.013, 0.013):
            mb.loft([R((x, -0.130, 0.030), 0.0135), R((x, -0.700, 0.030), 0.0125)], "steel", n=6, front=(0, 0, 1))
        mb.box((0, -0.690, 0.046), (0.006, 0.010, 0.008), "brass")
    else:
        mb.loft([R((0, -0.130, 0.034), 0.0125), R((0, -0.800, 0.034), 0.0105)], "steel", n=6, front=(0, 0, 1))
        mb.loft([R((0, -0.440, 0.010), 0.0095), R((0, -0.760, 0.012), 0.0095)], "steel_dark", n=6, front=(0, 0, 1))
        mb.box((0, -0.600, 0.022), (0.030, 0.014, 0.040), "steel_dark")
        mb.box((0, -0.790, 0.050), (0.005, 0.014, 0.012), "steel_dark")
        mb.box((0, -0.120, 0.050), (0.012, 0.016, 0.012), "steel_dark")
    mb.default_w = None


def dynamite(mb, w=None, sticks=3):
    """Bundle upright along +Z, origin at the middle of the bundle."""
    mb.default_w = w
    r = 0.017
    if sticks == 1:
        pos = [(0, 0)]
    else:
        pos = [(r * 1.05 * cos(2 * pi * k / sticks + 0.5), r * 1.05 * sin(2 * pi * k / sticks + 0.5)) for k in range(sticks)]
    for x, y in pos:
        mb.loft([R((x, y, -0.10), r), R((x, y, 0.10), r)], "dynamite", n=6)
        mb.box((x, y, 0.102), (0.012, 0.012, 0.004), "cream")
    if sticks > 1:
        for z in (-0.055, 0.055):
            mb.loft([R((0, 0, z - 0.008), r * 2.25), R((0, 0, z + 0.008), r * 2.25)], "rope", n=8, caps=(False, False))
    fx, fy = pos[0]
    mb.loft([R((fx, fy, 0.10), 0.003), R((fx + 0.010, fy - 0.006, 0.135), 0.003),
             R((fx + 0.030, fy - 0.014, 0.160), 0.003)], "fuse", n=4)
    c = Vector((fx + 0.032, fy - 0.015, 0.166))
    mb.poly([c + Vector(v) for v in ((0.016, 0, 0), (-0.016, 0, 0), (0, 0.016, 0), (0, -0.016, 0), (0, 0, 0.022),
                                     (0, 0, -0.014))],
            [(0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4), (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5)], "spark")
    mb.default_w = None


def gatling(mb, w_base=None, w_barrels=None, w_crank=None):
    """Gatling gun on a tripod. Origin on the ground under the pivot, barrels along -Y."""
    piv = Vector((0, 0, 1.05))
    mb.default_w = w_base
    for a in (90, 210, 330):
        foot = Vector((0.55 * cos(radians(a)), 0.55 * sin(radians(a)), 0.0))
        mb.loft([R(piv + Vector((0, 0, -0.12)), 0.030), R(foot, 0.022)], "wood_dark", n=6)
        mb.box(foot + Vector((0, 0, 0.01)), (0.09, 0.09, 0.02), "steel_dark")
    mb.loft([R(piv + Vector((0, 0, -0.16)), 0.055), R(piv + Vector((0, 0, -0.04)), 0.045)], "steel_dark", n=8)
    mb.box(piv + Vector((0, 0.05, 0.0)), (0.20, 0.10, 0.05), "brass")
    for x in (-0.095, 0.095):
        mb.box(piv + Vector((x, 0.05, 0.07)), (0.014, 0.08, 0.16), "brass")
    # breech housing + hopper + handles
    mb.loft([R(piv + Vector((0, 0.34, 0.10)), 0.075), R(piv + Vector((0, 0.30, 0.10)), 0.088),
             R(piv + Vector((0, 0.02, 0.10)), 0.088), R(piv + Vector((0, -0.02, 0.10)), 0.075)],
            "brass", n=10, front=(0, 0, 1))
    mb.box(piv + Vector((0, 0.16, 0.27)), (0.07, 0.11, 0.22), "steel_dark", taper=(1.0, 1.0))
    mb.box(piv + Vector((0, 0.16, 0.385)), (0.085, 0.125, 0.014), "steel")
    for x in (-0.05, 0.05):
        mb.loft([R(piv + Vector((x, 0.34, 0.10)), 0.011), R(piv + Vector((x, 0.46, 0.06)), 0.013)], "wood", n=6,
                front=(0, 0, 1))
    # barrels
    mb.default_w = w_barrels or w_base
    nb = 6
    for k in range(nb):
        a = 2 * pi * k / nb
        o = Vector((0.052 * cos(a), 0, 0.052 * sin(a)))
        mb.loft([R(piv + o + Vector((0, -0.02, 0.10)), 0.014), R(piv + o + Vector((0, -0.95, 0.10)), 0.012)],
                "steel", n=6, front=(0, 0, 1))
    for y in (-0.30, -0.62, -0.92):
        mb.loft([R(piv + Vector((0, y + 0.012, 0.10)), 0.074), R(piv + Vector((0, y - 0.012, 0.10)), 0.074)],
                "steel_dark", n=10, front=(0, 0, 1))
    # crank
    mb.default_w = w_crank or w_base
    c = piv + Vector((0.088, 0.20, 0.10))
    mb.loft([R(c, 0.012), R(c + Vector((0.06, 0, 0)), 0.012)], "steel_dark", n=6)
    mb.box(c + Vector((0.066, 0, -0.045)), (0.012, 0.022, 0.11), "steel_dark")
    mb.loft([R(c + Vector((0.066, 0, -0.09)), 0.013), R(c + Vector((0.15, 0, -0.09)), 0.015)], "wood", n=6)
    mb.default_w = None


def build_gatling(name="Gatling", location=(0, 0, 0)):
    """Rigged gatling: bones root / barrels / crank, with a looping fire action."""
    from .core import Poser, build_armature, make_action, rot
    coll = get_collection(name, get_collection("WW_Props"))
    arm = build_armature(name, [("root", None, (0, 0, 0), (0, 0.2, 0)), ("barrels", "root", (0, -0.02, 1.15), (0, -0.95, 1.15)),
                                ("crank", "root", (0.088, 0.20, 1.15), (0.154, 0.20, 1.15))], coll)
    mb = MB(name + "_mesh")
    gatling(mb, w_base={"root": 1}, w_barrels={"barrels": 1}, w_crank={"crank": 1})
    ob = mb.finish(coll, arm)
    P = Poser(arm)
    keys = [(f, {"barrels": rot(y=-60 * f / 12), "crank": rot(x=360 * f / 12 + 90)}) for f in range(13)]
    make_action(arm, P, "gatling_spin", keys, loop=True)
    arm.location = location
    arm["ww_kind"] = "gatling"
    return arm, [ob]


def standalone(name, fn, collection, scale=1.0, **kw):
    from mathutils import Matrix
    mb = MB(name)
    mb.xf = Matrix.Scale(scale, 4)
    fn(mb, **kw)
    return mb.finish(collection)
