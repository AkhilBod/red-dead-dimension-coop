"""FG world chunks, built to the team's Blender Chunk Design Guide.

Convention (Blender, metres): +X forward, origin at the start of the chunk on the main track, +Z up.
Blender is right-handed, so the driver's LEFT is +Y here. Unreal's FBX import flips Y, which gives the
guide's "+Y = right" in engine. Every chunk is 50 m = 5000 uu long.

How they join without gaps: a chunk is authored in track space (d = metres left of the main line,
s = metres travelled) and then bent along its centreline. Each end of a chunk is one of four JOINT
types with a fixed cross-section shared by every chunk that uses it:
    O   open desert, single line          (all the normal chunks: any order works)
    C   inside a canyon
    T   inside a tunnel
    OD  open desert with the enemy side track alongside
A chunk may follow another if its start joint equals the other's end joint. Curves ease in and out
with zero curvature at both ends, so position, heading and curvature all match at every joint.
"""
import json
import os
import random
from math import atan2, cos, degrees, pi, radians, sin

import bpy
from mathutils import Euler, Matrix, Vector, noise

from . import core, scenery, town, train
from .core import MB, R

LENGTH = 50.0
WIDTH = 200.0
SIDE_D = scenery.SECOND_TRACK_X      # the enemy side track runs this far to the left
EDGE = 10.0                          # metres over which a chunk blends into its joint's cross-section
ROW = 2.5
LANE = (10.0, 18.0)                  # riding lanes beside the train, from the guide
PIECE = 10.0                         # track piece length inside a chunk
TELE_D = 21.0                        # the telegraph line runs along the left side, outside the riding lane


def sstep(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


def ease(t):
    """0 -> 1 with zero slope and zero curvature at both ends."""
    t = max(0.0, min(1.0, t))
    return t - sin(2 * pi * t) / (2 * pi)


# ---------------------------------------------------------------- centreline

class Path:
    """kind: 'straight', 'curve' (turn by deg), 's' (swing out by deg and come back). deg > 0 turns left."""

    def __init__(self, kind="straight", deg=0.0, length=LENGTH, step=0.25):
        self.kind, self.deg, self.length, self.step = kind, deg, length, step
        a = radians(deg)
        self.pts, self.yaw = [Vector((0.0, 0.0))], [0.0]
        for i in range(1, int(round(length / step)) + 1):
            s = i * step
            th = a * ease(s / length) if kind == "curve" else a * 0.5 * (1 - cos(2 * pi * s / length)) if kind == "s" else 0.0
            mid = (th + self.yaw[-1]) / 2
            self.pts.append(self.pts[-1] + Vector((cos(mid), sin(mid))) * step)
            self.yaw.append(th)

    def at(self, s):
        if s <= 0:
            return Vector((s, 0.0)), 0.0
        if s >= self.length:
            th = self.yaw[-1]
            return self.pts[-1] + Vector((cos(th), sin(th))) * (s - self.length), th
        f = s / self.step
        i = min(int(f), len(self.pts) - 2)
        t = f - i
        return self.pts[i].lerp(self.pts[i + 1], t), self.yaw[i] + (self.yaw[i + 1] - self.yaw[i]) * t

    def world(self, d, s, z=0.0):
        p, th = self.at(s)
        return Vector((p.x - d * sin(th), p.y + d * cos(th), z))

    def warp(self, co):
        """Track space (x = d, y = -s) to the chunk's own space."""
        return self.world(co.x, -co.y, co.z)


# ---------------------------------------------------------------- ground

class Ground:
    def __init__(self, seed, joints, canyon=0.0, canyon_from=12.0, sides="both", hill=False, gulch=False, rough=1.0):
        self.seed, self.j0, self.j1 = seed, joints[0], joints[1]
        self.canyon, self.canyon_from, self.sides, self.hill, self.gulch, self.rough = canyon, canyon_from, sides, hill, gulch, rough
        self.bore = None         # (s0, s1) where the tunnel is

    def _amount(self, s, a0, a1, body):
        """How much of a feature there is at s: the joints' fixed amounts at the ends, `body` in between."""
        t = s / LENGTH
        if a0 == a1 == body:
            return body
        if a0 == a1:             # comes and goes inside the chunk
            return a0 + (body - a0) * sstep((s - 6) / 12.0) * sstep((LENGTH - 6 - s) / 12.0)
        return a0 + (a1 - a0) * sstep((t - (0.12 if a1 > a0 else 0.38)) / 0.5)      # rise early, fall late: full height at a tunnel mouth

    def h(self, d, s):
        e = sstep(min(s, LENGTH - s) / EDGE)
        z = scenery._h_raw(d, 0.0, 0, LENGTH) * (1 - e) + scenery._h_raw(d, s, self.seed, LENGTH) * e * self.rough
        n = noise.noise(Vector((d * 0.03, 1.7, 9.1))) * (1 - e) + noise.noise(Vector((d * 0.03, s * 0.03, self.seed * 5.1))) * e
        ad = abs(d)
        c = self._amount(s, float(self.j0 == "C"), float(self.j1 == "C"), self.canyon)
        if c > 0 and (self.sides == "both" or (self.sides == "left") == (d > 0)):
            f0 = 12.0 if self.j0 == "C" else max(self.canyon_from, 26.0)      # walls close in to 12 m only inside a deep canyon
            f1 = 12.0 if self.j1 == "C" else max(self.canyon_from, 26.0)
            start = f0 + (f1 - f0) * sstep(s / LENGTH)
            wall = sstep((ad - start) / 18.0) * (24.0 + 8.0 * n)
            z += c * (wall * 0.35 + 0.65 * round(wall / 6.0) * 6.0 * sstep((ad - start) / 8.0))
        t = self._amount(s, float(self.j0 == "T"), float(self.j1 == "T"), float(self.hill))
        if t > 0:
            hill = (22.0 + 5.0 * n) * t * (1.0 - 0.6 * sstep((ad - 40.0) / 60.0))
            inside = self.bore is not None and self.bore[0] <= s <= self.bore[1]
            z += hill * (1.0 if inside else sstep((ad - 5.6) / 4.5))
        if self.gulch:
            g = sstep((s - 9) / 10.0) * sstep((LENGTH - 9 - s) / 10.0)
            z = max(z - g * (11.0 + 1.5 * n) * (1.0 - 0.5 * sstep((ad - 50) / 50)), -9.5)
        return z

    def mat(self, d, s, z, side_bed=False):
        if z > -1.0 and (abs(d) < 3.6 or (side_bed and abs(d - SIDE_D) < 3.6)):
            return "dirt"
        if z <= -9.45:
            return "water"
        if z < -1.5:
            return ("rock_dark", "rock_red", "rock_orange")[int(-z / 3.0) % 3]
        if z > 3.2 and (self.canyon or self.hill or "C" in (self.j0, self.j1) or "T" in (self.j0, self.j1)):
            return ("rock_red", "rock_orange", "rock_pale", "rock_red", "rock_dark")[int(z / 5.0) % 5]
        t = noise.noise(Vector((d * 0.05, s * 0.05 + self.seed, self.seed * 1.7)))
        return "sand_dark" if (t < -0.18 or z > 6) else "sand_light" if t > 0.22 else "sand"


def build_ground(mb, g, side_bed=False):
    xs = [-WIDTH / 2]
    while xs[-1] < WIDTH / 2 - 1e-6:
        ad = abs(xs[-1] + 1.25)
        xs.append(xs[-1] + (2.5 if ad < 20 else 5.0 if ad < 50 else 10.0))
    ny = int(round(LENGTH / ROW))
    rng = random.Random(g.seed)
    grid = [[mb.vert((x + (0.0 if (j in (0, ny) or abs(x) < 13) else rng.uniform(-0.8, 0.8)), -j * ROW, g.h(x, j * ROW))) for j in range(ny + 1)] for x in xs]
    for i in range(len(xs) - 1):
        for j in range(ny):
            d, s = (xs[i] + xs[i + 1]) / 2, (j + 0.5) * ROW
            if g.bore and g.bore[0] <= s <= g.bore[1] and abs(d) < 5.6:
                continue
            m = g.mat(d, s, g.h(d, s), side_bed)
            a, b, c, e = grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]
            for tri in (((a, b, c), (a, c, e)) if (i + j) % 2 else ((a, b, e), (b, c, e))):
                f = mb.face(list(tri), m)
                if f:
                    f.tag = True


# ---------------------------------------------------------------- one chunk under construction

class Chunk:
    def __init__(self, name, path, ground, seed):
        self.name, self.path, self.g = name, path, ground
        self.rng = random.Random(seed * 13 + 1)
        self.parts = {k: MB(f"{name}_{k}") for k in ("Ground", "Track", "SceneryNear", "SceneryMid")}
        for mb in self.parts.values():
            mb.warp = path.warp
        self.rigs, self.events, self.markers = [], [], []

    def put(self, part, fn, kw, d, s, yaw=0.0, z=None, scale=1.0, sink=0.05):
        mb = self.parts[part]
        zz = self.g.h(d, s) - sink if z is None else z
        mb.xf = Matrix.Translation((d, -s, zz)) @ Euler((0, 0, yaw)).to_matrix().to_4x4() @ Matrix.Scale(scale, 4)
        fn(mb, **kw)
        mb.xf = None

    def rig(self, asset, d, s, yaw=0.0, z=0.0, action=None):
        self.rigs.append(dict(asset=asset, d=d, s=s, z=z, yaw=yaw, action=action))

    def marker(self, name, d, s):
        self.markers.append((name, d, s))

    def track(self, offset=None, s0=0.0, s1=LENGTH):
        """offset(s) = lateral position of this line. None = the main line."""
        mb = self.parts["Track"]
        if offset is not None:
            mb.warp = lambda co: self.path.world(co.x + offset(-co.y), -co.y, co.z)
        k = int(s0 / PIECE)
        while k * PIECE < s1 - 1e-6:
            a = k * PIECE
            d0 = offset(a + PIECE / 2) if offset else 0.0
            low = min(self.g.h(d0, a + t) for t in (0, 2.5, 5, 7.5, 10))
            mb.xf = Matrix.Translation((0, -(a + PIECE), 0))
            if low < -1.5:
                scenery.track_trestle(mb, length=PIECE, height=-low + 1.0)
            else:
                scenery.track_straight(mb, length=PIECE, seed=k + (7 if offset else 0))
            mb.xf = None
            k += 1
        mb.warp = self.path.warp

    def telegraph(self):
        """Poles at s = 12.5 and 37.5, never on a joint. A pole's wires run BACK 25 m, so the first pole's wires
        cross the start joint and land on the previous chunk's second pole. Canyon and tunnel joints carry no line."""
        if self.g.j0 in ("O", "OD"):
            self.put("SceneryNear", scenery.telegraph_span, dict(span=25.0), TELE_D, 12.5, sink=0.0)
        if self.g.j1 in ("O", "OD"):
            self.put("SceneryNear", scenery.telegraph_span, dict(span=25.0, wires=self.g.j0 in ("O", "OD")), TELE_D, 37.5, sink=0.0)

    def scatter(self, count, names=None, reach=95.0, inner=None, keep_out=(), part="SceneryNear"):
        names = names or [n for n in scenery.SCATTER if n not in ("Fence", "WagonWheel")]
        inner = LANE[1] + 2.0 if inner is None else inner
        for _ in range(count):
            nm = self.rng.choice(names)
            d, s = self.rng.uniform(-reach, reach), self.rng.uniform(4.0, LENGTH - 4.0)       # joints stay neutral
            z = self.g.h(d, s)
            steep = abs(self.g.h(d + 2, s) - z) + abs(self.g.h(d, s + 2) - z)
            if abs(d) < inner or z < -0.8 or steep > 1.6 or any(a <= s <= b and c <= d <= e for a, b, c, e in keep_out):
                continue
            if abs(d - TELE_D) < 2.5:
                continue                                                                       # under the telegraph wires
            fn, kw = scenery.SCATTER[nm]
            self.put(part, fn, kw, d, s, yaw=self.rng.uniform(0, 6.28), scale=self.rng.uniform(0.8, 1.35))

    def landmarks(self, spots):
        for nm, d, s, scale in spots:
            fn, kw = scenery.LANDMARKS[nm]
            self.put("SceneryMid", fn, kw, d, s, yaw=self.rng.uniform(0, 6.28), scale=scale, sink=1.0)

    def encounters(self, left=True, right=True, ahead=True):
        mid = (LANE[0] + LANE[1]) / 2
        for ok, nm, sign in ((left, "Left", 1), (right, "Right", -1)):
            if ok:
                self.marker(f"Encounter_{nm}_01", sign * mid, 15.0)
                self.marker(f"Encounter_{nm}_02", sign * mid, 35.0)
        if ahead:
            self.marker("Encounter_Ahead_01", 0.0, 45.0)


def tunnel(c, s0, s1, mouth_at=()):
    """Single-track bore in track space, so it bends with the line. mouth_at: s values that get a rock portal."""
    mb = c.parts["SceneryNear"]
    rx, top = 5.0, 7.2
    n = max(2, int(round((s1 - s0) / 5.0)))
    ys = [s0 + k * (s1 - s0) / n for k in range(n + 1)]
    mb.loft([R((0, -s, 0.0), rx, top, 0.0) for s in ys], "rock_dark", n=10, arc=(-90, 90), thick=-0.9, front=(0, 0, 1))
    for s in ys:
        if 2.0 < s < LENGTH - 2.0:
            for sx in (-1, 1):
                mb.box((sx * (rx - 0.3), -s, top * 0.36), (0.34, 0.34, top * 0.72), "plank_dark")
            mb.box((0, -s, top * 0.745), (rx * 1.5, 0.34, 0.34), "plank_dark")
    for s in mouth_at:
        out = -1 if s == s0 else 1
        W, H, T, seg = 14.0, max(c.g.h(9.0, s), c.g.h(-9.0, s), top + 3.0) + 1.5, 2.0, 12
        for k in range(seg):
            quad = []
            for a in (radians(-90 + 180 * k / seg), radians(-90 + 180 * (k + 1) / seg)):
                sa, ca = sin(a), max(cos(a), 1e-4)
                t = min(W / max(abs(sa), 1e-4), H / ca)
                quad.append((Vector((rx * sa, 0, top * ca)), Vector((t * sa, 0, t * ca))))
            (p0, o0), (p1, o1) = quad
            pts = [v + Vector((0, -s - out * dy, 0)) for dy in (0.0, T) for v in (p0, p1, o1, o0)]
            mb.poly(pts, [(0, 1, 2, 3), (7, 6, 5, 4), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)], ("rock_red", "rock_dark", "rock_orange")[k % 3])
        y = -s - out * 0.45
        for sx in (-1, 1):
            mb.box((sx * (rx + 0.1), y, town.CLEAR_H / 2), (0.6, 0.7, town.CLEAR_H), "plank_dark")
        mb.box((0, y, town.CLEAR_H + 0.35), (rx * 2 + 1.6, 0.8, 0.7), "plank_dark")
        mb.box((0, y - out * 0.42, town.CLEAR_H + 0.35), (2.6, 0.08, 0.46), "paint_white")
        c.events.append(dict(kind="duck", s=s, what="tunnel mouth"))


# ---------------------------------------------------------------- recipes

BUSH = ["Sagebrush_A", "Sagebrush_B", "Rock_C", "Tumbleweed", "BarrelCactus"]


def _flat(c):
    c.scatter(26, names=BUSH + ["Saguaro_B", "PricklyPear_A"])
    c.scatter(4, names=["Saguaro_A", "Saguaro_C"])
    c.landmarks([("Butte_A", 78, 27, 0.7)] if c.rng.random() < 0.5 else [("Mesa_C", -82, 24, 0.8)])
    c.encounters()


def _rocky(c):
    c.scatter(34, names=["Rock_A", "Rock_B", "Rock_C", "RockCluster_A", "Sagebrush_A", "DeadTree_A"])
    c.scatter(7, names=["RockCluster_B", "Rock_B"], inner=26.0, part="SceneryMid")
    c.landmarks([("Butte_B", -70, 30, 0.6)])
    c.encounters()


def _cactus(c):
    c.scatter(70, names=["Saguaro_A", "Saguaro_B", "Saguaro_C", "Saguaro_A", "BarrelCactus", "PricklyPear_A", "PricklyPear_B", "Sagebrush_B"])
    c.encounters()


def _boneyard(c):
    c.scatter(34, names=["DeadTree_A", "DeadTree_B", "CowSkull", "Rock_A", "Rock_C", "Tumbleweed", "Sagebrush_A"])
    c.put("SceneryNear", scenery.cow_skull, {}, 7.0, 22.0, yaw=0.8, scale=1.6)
    c.put("SceneryNear", scenery.cow_skull, {}, -8.0, 31.0, yaw=2.4, scale=1.4)
    c.encounters()


def _fence(c):
    for k in range(6):
        c.put("SceneryNear", scenery.fence, {}, 27.0, 8.0 + k * 6.0, yaw=pi / 2)
    c.put("SceneryNear", town.signpost, {}, 8.5, 25.0, yaw=pi / 2)
    c.put("SceneryNear", town.wanted_board, {}, -8.5, 30.0, yaw=-pi / 2)
    c.scatter(26, names=BUSH, keep_out=[(4, 46, 24, 30)])
    c.encounters()


def _canyon_open(c):
    c.scatter(22, names=BUSH + ["Rock_A", "Rock_B"], reach=24.0)
    c.encounters()


def _canyon_inside(c):
    c.scatter(14, names=["Rock_A", "Rock_C", "Sagebrush_A", "RockCluster_A"], reach=13.0, inner=6.5)
    c.events.append(dict(kind="narrow", s0=0, s1=LENGTH))
    c.encounters(left=False, right=False)


def _mesa(c):
    c.scatter(30)
    c.landmarks([("Mesa_A", -62, 20, 0.62), ("Butte_B", 58, 34, 0.7)])
    c.encounters()


def _arch(c):
    c.scatter(26)
    c.put("SceneryMid", scenery.rock_arch, dict(seed=5, span=44.0, height=21.0), 0.0, 25.0, yaw=0.0, z=-0.5)
    c.events.append(dict(kind="overhead", s=25))
    c.encounters(left=False, right=False)


def _gulch(c):
    c.scatter(24)
    c.events.append(dict(kind="trestle", s0=12, s1=38))
    c.encounters(left=False, right=False, ahead=True)


def _shack(c):
    fn, kw = town.BUILDINGS["Shack"]
    c.put("SceneryNear", fn, kw, -27.0, 26.0, yaw=-pi / 2 + 0.25, sink=0.15)
    c.put("SceneryNear", town.BUILDINGS["Outhouse"][0], {}, -36.0, 17.0, yaw=1.0)
    c.put("SceneryNear", scenery.dead_tree, dict(seed=8, h=5.5), -22.0, 17.0)
    c.put("SceneryNear", scenery.wagon_wheel, {}, -24.2, 29.5, yaw=0.3)
    for k in range(3):
        c.put("SceneryNear", scenery.fence, {}, -20.0, 30.0 + k * 5.5, yaw=pi / 2)
    c.scatter(24, keep_out=[(8, 44, -42, -18)])
    c.encounters(right=False)


def _camp(c):
    c.put("SceneryNear", town.wagon, {}, 32.0, 24.0, yaw=0.6)
    for d, s in ((36.0, 30.5), (37.0, 31.5)):
        c.put("SceneryNear", town.PROPS["Crate"][0], {}, d, s, yaw=d)
    c.put("SceneryNear", town.PROPS["Barrel"][0], {}, 34.5, 31.5)
    c.put("SceneryNear", town.sack, {}, 33.5, 29.5)
    c.put("SceneryNear", town.hay_bale, {}, 38.0, 22.0, yaw=0.4)
    for k in range(7):                         # fire ring
        a = 2 * pi * k / 7
        c.put("SceneryNear", scenery.rock, dict(seed=20 + k, s=0.22, mat="rock_grey"), 28.0 + 0.7 * cos(a), 31.0 + 0.7 * sin(a))
    c.put("SceneryNear", town.lantern, {}, 29.6, 29.2, scale=1.4)
    c.scatter(24, keep_out=[(14, 40, 24, 44)])
    c.encounters(left=False)


def _water_tower(c):
    c.rig("WaterTower", -5.5, 25.0, yaw=pi, action="WaterTower_spout_swing")
    c.put("SceneryNear", town.PROPS["Barrel"][0], {}, -9.0, 29.0)
    c.put("SceneryNear", town.PROPS["Barrel"][0], {}, -9.8, 30.0)
    c.put("SceneryNear", town.PROPS["Crate"][0], {}, -10.0, 21.0, yaw=0.3)
    c.scatter(24, keep_out=[(15, 35, -14, -2)])
    c.events.append(dict(kind="duck", s=25, what="water tower spout"))
    c.encounters(right=False)


def _gantry(c):
    c.put("SceneryNear", town.low_gantry, {}, 0.0, 25.0, z=0.0)
    c.scatter(26)
    c.events.append(dict(kind="duck", s=25, what="signal gantry"))
    c.encounters()


def _station(c):
    c.put("SceneryNear", town.station, {}, -9.6, 25.0, yaw=pi / 2, z=0.0)
    for nm, s in (("GeneralStore", 38), ("Sheriff", 26), ("Hotel", 13)):
        fn, kw = town.BUILDINGS[nm]
        c.put("SceneryMid", fn, kw, -46.0, s, yaw=pi / 2, sink=0.2)
    for nm, d, s, yaw, z in (("Trough", -38, 32, pi / 2, None), ("HitchingPost", -38.5, 20, pi / 2, None), ("BottleCrate", -4.0, 30, pi / 2, 1.0),
                             ("StartSign", -18, 44, pi / 2, None), ("Wagon", -30, 8, 0.5, None)):
        c.put("SceneryNear", town.PROPS[nm][0], town.PROPS[nm][1], d, s, yaw=yaw, z=z)
    c.rig("StationBell", -4.2, 19.0, yaw=pi / 2, z=1.0, action="StationBell_ring")
    c.rig("Windmill", -70.0, 30.0, yaw=0.6, action="Windmill_spin")
    c.scatter(14, keep_out=[(0, 50, -80, -2)])
    c.events.append(dict(kind="station", s=25))
    c.encounters(right=False)


def _tunnel_entry(c):
    tunnel(c, 24.0, LENGTH, mouth_at=(24.0,))
    c.scatter(18, keep_out=[(12, 50, -60, 60)])
    c.events.append(dict(kind="dark", s0=24, s1=LENGTH))
    c.encounters(left=False, right=False, ahead=False)


def _tunnel_mid(c):
    tunnel(c, 0.0, LENGTH)
    c.events.append(dict(kind="dark", s0=0, s1=LENGTH))


def _tunnel_exit(c):
    tunnel(c, 0.0, 26.0, mouth_at=(26.0,))
    c.scatter(18, keep_out=[(0, 38, -60, 60)])
    c.events.append(dict(kind="dark", s0=0, s1=26))
    c.encounters(left=False, right=False)


def _side(kind):
    def f(c):
        off = {"start": lambda s: SIDE_D * ease(s / LENGTH), "mid": lambda s: SIDE_D, "end": lambda s: SIDE_D * (1 - ease(s / LENGTH))}[kind]
        c.track(offset=off)
        if kind == "start":
            c.put("SceneryNear", town.lamp_post, dict(lit=True), -3.6, 6.0)
        c.scatter(24, inner=26.0)
        c.marker("EnemyTrain_01", SIDE_D, 25.0)
        c.events.append(dict(kind="side_track", s0=0, s1=LENGTH, d=SIDE_D))
        c.encounters(left=False)
    return f


# name: (joints, shape, ground options, recipe, telegraph spans, weight for random selection)
RECIPES = {
    "FG_Chunk_Flat_A":               (("O", "O"), ("straight", 0), dict(rough=0.7), _flat, (0, 25), 3),
    "FG_Chunk_Flat_B":               (("O", "O"), ("straight", 0), dict(rough=0.7), _flat, (0, 25), 3),
    "FG_Chunk_Rocky_A":              (("O", "O"), ("straight", 0), {}, _rocky, (0, 25), 3),
    "FG_Chunk_Rocky_B":              (("O", "O"), ("straight", 0), {}, _rocky, (0, 25), 2),
    "FG_Chunk_Canyon_A":             (("O", "O"), ("straight", 0), dict(canyon=1.0, canyon_from=26.0), _canyon_open, (), 2),
    "FG_Chunk_Canyon_B":             (("O", "O"), ("straight", 0), dict(canyon=1.0, canyon_from=26.0, sides="left"), _canyon_open, (0, 25), 2),
    "FG_Chunk_Cactus_A":             (("O", "O"), ("straight", 0), {}, _cactus, (0, 25), 2),
    "FG_Chunk_Boneyard_A":           (("O", "O"), ("straight", 0), {}, _boneyard, (0, 25), 1),
    "FG_Chunk_Telegraph_Fence_A":    (("O", "O"), ("straight", 0), dict(rough=0.7), _fence, (0, 25), 2),
    "FG_Chunk_Mesa_A":               (("O", "O"), ("straight", 0), {}, _mesa, (0, 25), 2),
    "FG_Chunk_CurveL_A":             (("O", "O"), ("curve", 10), {}, _flat, (0, 25), 2),
    "FG_Chunk_CurveL_B":             (("O", "O"), ("curve", 10), {}, _rocky, (0, 25), 2),
    "FG_Chunk_CurveR_A":             (("O", "O"), ("curve", -10), {}, _cactus, (0, 25), 2),
    "FG_Chunk_CurveR_B":             (("O", "O"), ("curve", -10), {}, _mesa, (0, 25), 2),
    "FG_Chunk_SBendL_A":             (("O", "O"), ("s", 6), {}, _rocky, (0, 25), 1),
    "FG_Chunk_SBendR_A":             (("O", "O"), ("s", -6), {}, _flat, (0, 25), 1),
    "FG_Chunk_Landmark_Shack_A":     (("O", "O"), ("straight", 0), {}, _shack, (), 0.5),
    "FG_Chunk_Landmark_Camp_A":      (("O", "O"), ("straight", 0), {}, _camp, (0, 25), 0.5),
    "FG_Chunk_Landmark_WaterTower_A": (("O", "O"), ("straight", 0), {}, _water_tower, (0, 25), 0.5),
    "FG_Chunk_Landmark_Gantry_A":    (("O", "O"), ("straight", 0), {}, _gantry, (0, 25), 0.5),
    "FG_Chunk_Landmark_Arch_A":      (("O", "O"), ("straight", 0), {}, _arch, (), 0.4),
    "FG_Chunk_Landmark_Station_A":   (("O", "O"), ("straight", 0), dict(rough=0.5), _station, (), 0.3),
    "FG_Chunk_Gulch_A":              (("O", "O"), ("straight", 0), dict(gulch=True), _gulch, (), 0.6),
    "FG_Chunk_Gulch_CurveR_A":       (("O", "O"), ("curve", -8), dict(gulch=True), _gulch, (), 0.4),
    # sets: enter, any number of middles, leave
    "FG_Chunk_CanyonDeep_Entry_A":   (("O", "C"), ("straight", 0), dict(canyon=1.0), _canyon_open, (), 0.5),
    "FG_Chunk_CanyonDeep_Mid_A":     (("C", "C"), ("straight", 0), dict(canyon=1.0), _canyon_inside, (), 1),
    "FG_Chunk_CanyonDeep_CurveL_A":  (("C", "C"), ("curve", 10), dict(canyon=1.0), _canyon_inside, (), 1),
    "FG_Chunk_CanyonDeep_CurveR_A":  (("C", "C"), ("curve", -10), dict(canyon=1.0), _canyon_inside, (), 1),
    "FG_Chunk_CanyonDeep_Exit_A":    (("C", "O"), ("straight", 0), dict(canyon=1.0), _canyon_open, (), 1),
    "FG_Chunk_Tunnel_Entry_A":       (("O", "T"), ("straight", 0), dict(hill=True), _tunnel_entry, (), 0.4),
    "FG_Chunk_Tunnel_Mid_A":         (("T", "T"), ("straight", 0), dict(hill=True), _tunnel_mid, (), 1),
    "FG_Chunk_Tunnel_CurveL_A":      (("T", "T"), ("curve", 10), dict(hill=True), _tunnel_mid, (), 1),
    "FG_Chunk_Tunnel_Exit_A":        (("T", "O"), ("straight", 0), dict(hill=True), _tunnel_exit, (), 1),
    "FG_Chunk_SideTrack_Start_A":    (("O", "OD"), ("straight", 0), dict(rough=0.7), _side("start"), (0, 25), 0.4),
    "FG_Chunk_SideTrack_Mid_A":      (("OD", "OD"), ("straight", 0), dict(rough=0.7), _side("mid"), (0, 25), 2),
    "FG_Chunk_SideTrack_CurveL_A":   (("OD", "OD"), ("curve", 10), dict(rough=0.7), _side("mid"), (0, 25), 1),
    "FG_Chunk_SideTrack_CurveR_A":   (("OD", "OD"), ("curve", -10), dict(rough=0.7), _side("mid"), (0, 25), 1),
    "FG_Chunk_SideTrack_End_A":      (("OD", "O"), ("straight", 0), dict(rough=0.7), _side("end"), (0, 25), 1),
}
S = "FG_Chunk_"
DEMO_ORDER = [S + n for n in (
    "Landmark_Station_A", "Flat_A", "CurveL_A", "Rocky_A", "Canyon_A", "CurveR_A", "CurveR_B", "Landmark_WaterTower_A", "SBendL_A",
    "SideTrack_Start_A", "SideTrack_Mid_A", "SideTrack_CurveL_A", "SideTrack_Mid_A", "SideTrack_End_A", "Gulch_A", "Cactus_A",
    "CanyonDeep_Entry_A", "CanyonDeep_CurveL_A", "CanyonDeep_Mid_A", "CanyonDeep_CurveR_A", "CanyonDeep_Exit_A", "Landmark_Camp_A", "CurveL_B",
    "Tunnel_Entry_A", "Tunnel_CurveL_A", "Tunnel_Mid_A", "Tunnel_Exit_A", "Landmark_Arch_A", "Mesa_A", "SBendR_A", "Boneyard_A",
    "Landmark_Gantry_A", "Telegraph_Fence_A", "Flat_B")]
GUIDE_TEST = [S + n for n in ("Flat_A", "Canyon_A", "Rocky_A", "Flat_A", "Rocky_A", "Canyon_A")]


def build_chunk(name, coll):
    joints, (kind, deg), gopts, recipe, spans, weight = RECIPES[name]
    seed = (sum(ord(ch) * (i + 1) for i, ch in enumerate(name)) % 89) + 1
    path = Path(kind, deg)
    g = Ground(seed, joints, **gopts)
    if "Tunnel" in name:
        g.bore = {"Entry": (24.0, LENGTH), "Exit": (0.0, 26.0)}.get(name.split("_")[3], (0.0, LENGTH))
    c = Chunk(name, path, g, seed)
    build_ground(c.parts["Ground"], g, side_bed="SideTrack" in name)
    c.track()
    recipe(c)
    c.telegraph()
    root = bpy.data.objects.new(name, None)
    root.empty_display_size = 2.0
    coll.objects.link(root)
    meshes = []
    for key, mb in c.parts.items():
        if len(mb.bm.verts):
            ob = mb.finish(coll)
            ob.parent = root
            meshes.append(ob)
        else:
            mb.bm.free()

    def empty(nm, loc, yaw=0.0, kind="ARROWS", size=1.5):
        e = bpy.data.objects.new(f"{name}.{nm}", None)
        e.empty_display_type, e.empty_display_size = kind, size
        e.location, e.rotation_euler = loc, (0, 0, yaw)
        e.parent = root
        e["ww_socket"] = nm
        coll.objects.link(e)
        return e
    p, th = path.at(LENGTH)
    empty("Connector_Start", (0, 0, 0))
    empty("Connector_End", (p.x, p.y, 0), th)
    marks = []
    for nm, d, s in c.markers:
        w = path.world(d, s, max(0.0, g.h(d, s)))
        _, yaw = path.at(s)
        empty(nm, tuple(w), yaw, kind="SPHERE", size=1.0)
        marks.append(dict(name=nm, x=round(w.x, 3), y=round(w.y, 3), z=round(w.z, 3), yaw_deg=round(degrees(yaw), 2)))
    rigs = []
    for r in c.rigs:
        w = path.world(r["d"], r["s"], r["z"])
        _, yaw = path.at(r["s"])
        # set pieces are authored facing -Y with the line along Y; the chunk's line runs along +X
        rigs.append(dict(asset=r["asset"], action=r["action"], x=round(w.x, 3), y=round(w.y, 3), z=round(w.z, 3), yaw_deg=round(degrees(yaw + r["yaw"] + pi / 2), 2)))
    line = []
    for k in range(int(LENGTH / 5) + 1):
        q, yaw = path.at(k * 5.0)
        line.append([round(q.x, 3), round(q.y, 3), round(degrees(yaw), 3)])
    meta = dict(name=name, file="SM_" + name + ".fbx", start_joint=joints[0], end_joint=joints[1], shape=kind, weight=weight, length_m=LENGTH,
                end_connector=dict(x=round(p.x, 4), y=round(p.y, 4), z=0.0, yaw_deg=round(degrees(th), 4)),
                centreline_every_5m=line, markers=marks, rigs=rigs, events=c.events,
                tris=sum(len(o.data.polygons) for o in meshes))
    root["ww_chunk"] = json.dumps(meta)
    return root, meta


# ---------------------------------------------------------------- chaining (what the Unreal spawner does)

def chain(order, metas):
    out, pos, yaw = [], Vector((0.0, 0.0)), 0.0
    for a, b in zip(order, order[1:]):
        if metas[a]["end_joint"] != metas[b]["start_joint"]:
            raise ValueError(f"{a} ends in joint {metas[a]['end_joint']} but {b} starts with {metas[b]['start_joint']}")
    for nm in order:
        out.append((pos.x, pos.y, yaw))
        e = metas[nm]["end_connector"]
        pos = pos + Vector((e["x"] * cos(yaw) - e["y"] * sin(yaw), e["x"] * sin(yaw) + e["y"] * cos(yaw)))
        yaw += radians(e["yaw_deg"])
    return out


def random_run(metas, n, seed=1):
    """A legal random sequence: weighted pick among chunks whose start joint fits."""
    rng, order, joint = random.Random(seed), [], "O"
    for _ in range(n):
        fits = [m for m in metas.values() if m["start_joint"] == joint and (not order or m["name"] != order[-1] or joint != "O")]
        pick = rng.choices(fits, weights=[m["weight"] for m in fits])[0]
        order.append(pick["name"])
        joint = pick["end_joint"]
    return order


def run_line(order, metas, entries, d=0.0):
    pts = []
    for i, (nm, (ex, ey, eyaw)) in enumerate(zip(order, entries)):
        for k, (x, y, ydeg) in enumerate(metas[nm]["centreline_every_5m"]):
            if i and k == 0:
                continue
            yw = eyaw + radians(ydeg)
            pts.append((i * LENGTH + k * 5.0, Vector((ex + x * cos(eyaw) - y * sin(eyaw) - d * sin(yw), ey + x * sin(eyaw) + y * cos(eyaw) + d * cos(yw))), yw))
    return pts


def _on_line(line, dist):
    for (s0, p0, y0), (s1, p1, y1) in zip(line, line[1:]):
        if s0 <= dist <= s1:
            t = (dist - s0) / (s1 - s0)
            return p0.lerp(p1, t)
    return line[-1][1]


def place_train(kinds, livery, line, front_dist, prefix, coll_hide=False):
    """Preview only. Each vehicle sits on the chord between its two trucks, like the real thing."""
    dist = front_dist
    for i, k in enumerate(kinds):
        f, b = train.LENGTH[k]
        dist -= f
        arm, ob = train.build_vehicle(k, livery, name=f"{prefix}_{i}_{k}")
        a, c = _on_line(line, dist + f * 0.55), _on_line(line, dist - b * 0.55)
        arm.location = ((a.x + c.x) / 2, (a.y + c.y) / 2, scenery.RAIL_TOP)
        arm.rotation_euler = (0, 0, atan2(a.y - c.y, a.x - c.x) + pi / 2)       # vehicles face -Y
        core.set_action(arm, bpy.data.actions[arm.name + "_roll"])
        dist -= b


def lay_run(order, metas, roots, coll, origin=(0.0, 0.0), rigs=True):
    entries = chain(order, metas)
    for nm, (ex, ey, eyaw) in zip(order, entries):
        ex, ey = ex + origin[0], ey + origin[1]
        for ob in roots[nm].children:
            if ob.type == "MESH":
                inst = bpy.data.objects.new("Run_" + ob.name, ob.data)
                coll.objects.link(inst)
                inst.location, inst.rotation_euler = (ex, ey, 0), (0, 0, eyaw)
        for r in (metas[nm]["rigs"] if rigs else ()):
            arm, _ = town.RIGGED[r["asset"]]()
            arm.name = "Run_" + r["asset"]
            arm.location = (ex + r["x"] * cos(eyaw) - r["y"] * sin(eyaw), ey + r["x"] * sin(eyaw) + r["y"] * cos(eyaw), r["z"])
            arm.rotation_euler = (0, 0, eyaw + radians(r["yaw_deg"]))
            if r["action"]:
                core.set_action(arm, bpy.data.actions[r["action"]])
    return [(x + origin[0], y + origin[1], yaw) for x, y, yaw in entries]


def build(with_trains=True):
    core.wipe()
    sc = bpy.context.scene
    sc.render.fps = core.FPS
    sc.frame_start, sc.frame_end = 0, 48
    lib = core.get_collection("FG_Chunks")
    metas, roots = {}, {}
    for i, nm in enumerate(RECIPES):
        root, meta = build_chunk(nm, lib)
        root.location = (3000.0 + (i % 8) * (LENGTH + 40), -(i // 8) * (WIDTH + 40), 0)      # library grid, away from the runs
        metas[nm], roots[nm] = meta, root
    entries = lay_run(DEMO_ORDER, metas, roots, core.get_collection("FG_Run_Demo"))
    lay_run(GUIDE_TEST, metas, roots, core.get_collection("FG_Run_GuideTest"), origin=(0.0, -700.0), rigs=False)
    if with_trains:
        main = run_line(DEMO_ORDER, metas, entries, 0.0)
        place_train(["Locomotive", "Tender", "PassengerCar", "Boxcar", "Flatcar_Crates", "PassengerCar", "Caboose"], "player", main, 170.0, "Run_Player")
        k = DEMO_ORDER.index(S + "SideTrack_Mid_A")
        place_train(["Locomotive", "Tender", "Flatcar_Gatling", "PassengerCar"], "bandit", run_line(DEMO_ORDER, metas, entries, SIDE_D), (k + 3) * LENGTH - 4.0, "Run_Bandit")
    return metas, entries


def check_seams(metas):
    """Measure every joint: worst height gap between any two chunks that may follow each other."""
    worst = {}
    prof = {}
    for nm in RECIPES:
        joints, (kind, deg), gopts, *_ = RECIPES[nm]
        seed = (sum(ord(ch) * (i + 1) for i, ch in enumerate(nm)) % 89) + 1
        g = Ground(seed, joints, **gopts)
        ds = [-WIDTH / 2 + k * 2.5 for k in range(int(WIDTH / 2.5) + 1)]
        prof[nm] = ([g.h(d, 0.0) for d in ds], [g.h(d, LENGTH) for d in ds])
    for a in RECIPES:
        for b in RECIPES:
            if metas[a]["end_joint"] == metas[b]["start_joint"]:
                gap = max(abs(x - y) for x, y in zip(prof[a][1], prof[b][0]))
                j = metas[a]["end_joint"]
                worst[j] = max(worst.get(j, 0.0), gap)
    return worst


def export_all(outdir, metas):
    d = os.path.join(outdir, "chunks")
    os.makedirs(d, exist_ok=True)
    for nm in RECIPES:
        root = bpy.data.objects[nm]
        kids = list(root.children)
        loc = root.location.copy()
        root.location = (0, 0, 0)
        ground = next(o for o in kids if o.name.endswith("_Ground"))
        renamed = []
        for e in kids:
            if e.type == "EMPTY":                     # Unreal turns SOCKET_* empties under a mesh into static mesh sockets
                renamed.append((e, e.name, e.parent))
                e.name = "SOCKET_" + e["ww_socket"]
                e.parent = ground
        bpy.context.view_layer.update()
        for o in bpy.context.view_layer.objects:
            o.select_set(False)
        for o in kids:
            o.select_set(True)
        bpy.context.view_layer.objects.active = ground
        try:
            bpy.ops.export_scene.fbx(filepath=os.path.join(d, "SM_" + nm + ".fbx"), use_selection=True, object_types={"MESH", "EMPTY"}, bake_anim=False,
                                     mesh_smooth_type="FACE", axis_forward="-Y", axis_up="Z")
        finally:
            for e, old, par in renamed:
                e.name, e.parent = old, par
            root.location = loc
    doc = dict(
        units="metres in Blender axes: +X forward, +Y = driver's LEFT, +Z up. Unreal's FBX import flips Y, so in engine +Y is the right side and yaw_deg changes sign.",
        length_m=LENGTH, length_uu=LENGTH * 100, rail_top_m=scenery.RAIL_TOP, side_track_d_m=SIDE_D, riding_lane_m=list(LANE),
        joints={"O": "open desert, single line", "C": "inside a deep canyon", "T": "inside a tunnel", "OD": "open desert with the enemy side track"},
        rule="chunk B may follow chunk A when B.start_joint == A.end_joint. Spawn B at A's Connector_End (position and yaw).",
        guide_test_order=GUIDE_TEST, demo_order=DEMO_ORDER, chunks=[metas[n] for n in RECIPES])
    with open(os.path.join(d, "chunks.json"), "w") as f:
        json.dump(doc, f, indent=1)
    return d
