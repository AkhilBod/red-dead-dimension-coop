"""Shared helpers: palette, mesh builder (lofts/boxes with skin weights), armature, poser, actions."""
import math
from math import sin, cos, radians, pi

import bmesh
import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

FPS = 30

PAL = {
    # skin
    "skin_tan": "#C98E6B", "skin_light": "#E0A883", "skin_dark": "#8A5A3C", "skin_red": "#B9694F",
    # hair
    "hair_black": "#2A1F1C", "hair_brown": "#4A2F22", "hair_grey": "#8C8378", "hair_ginger": "#8A4422",
    # cloth
    "red": "#A8402F", "red_faded": "#B9604A", "blue_dark": "#2F3E55", "blue_faded": "#5B7290",
    "cream": "#E8D4AE", "tan": "#C9A578", "brown": "#6E4A33", "brown_dark": "#46302A",
    "charcoal": "#34302F", "black": "#1F1B1B", "maroon": "#6B3340", "plum": "#5A3645",
    "olive": "#6A6A3F", "mustard": "#C79A3A", "white": "#EFE8DA", "grey": "#8E8A84",
    # leather / metal / wood
    "leather": "#7A4B2A", "leather_dark": "#4E2F1D", "leather_light": "#A66E3F",
    "steel": "#8D9198", "steel_dark": "#4B4E55", "brass": "#D1A64A", "wood": "#7B4A2B", "wood_dark": "#52301C",
    # misc
    "dynamite": "#C2352B", "fuse": "#D8C9A0", "spark": "#FFD36B", "eye": "#1A1414", "bone": "#E9DFC8",
    "rope": "#B89A66",
    # world
    "sand": "#E2BC85", "sand_light": "#EDCF9C", "sand_dark": "#CBA06A", "dirt": "#B08A5E", "gravel": "#8B7F72",
    "rock_red": "#B5603C", "rock_orange": "#CC7A45", "rock_pale": "#D9A06B", "rock_dark": "#7E3F2B", "rock_grey": "#8A7B6E",
    "cactus": "#5E7F4A", "cactus_dark": "#47643A", "sage": "#8C9670", "flower": "#D8567A", "deadwood": "#6B5745",
    "plank": "#9A6B43", "plank_dark": "#6F4A2E", "plank_light": "#B98A5A", "plank_grey": "#8A7A68", "paint_white": "#E4DAC4",
    "paint_red": "#9C3B2E", "paint_green": "#3F5A47", "paint_blue": "#4A6378", "paint_yellow": "#D0A446",
    "shingle": "#5A4136", "tin": "#9AA0A2", "glass": "#2B3A44", "glass_lit": "#F2C66B", "iron": "#2A2A2D", "iron_red": "#7E2B22",
    "coal": "#1B1A1C", "canvas": "#D8C8A4", "bottle_green": "#3F7A55", "bottle_brown": "#7A4A22", "paper": "#E2D3AE",
    "tracer": "#FFE08A", "flash": "#FFB347", "lead": "#6D6A70", "water": "#5F8FA0",
    # horse coats
    "horse_bay": "#7A4527", "horse_chestnut": "#9A5A30", "horse_black": "#2B2422", "horse_grey": "#A9A39A",
    "horse_palomino": "#D3A868", "horse_white": "#E7E0D2", "hoof": "#2E2722", "mane_black": "#1E1917",
    "mane_cream": "#E9DDBF", "mane_brown": "#3E261A",
}


def _lin(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hexcol(h):
    h = h.lstrip("#")
    return tuple(_lin(int(h[i:i + 2], 16) / 255) for i in (0, 2, 4)) + (1.0,)


def get_mat(key):
    name = "WW_" + key
    m = bpy.data.materials.get(name)
    if m:
        return m
    m = bpy.data.materials.new(name)
    col = hexcol(PAL[key])
    m.diffuse_color = col
    try:
        m.use_nodes = True
    except Exception:
        pass
    nt = m.node_tree
    if nt:
        bsdf = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if bsdf:
            bsdf.inputs["Base Color"].default_value = col
            bsdf.inputs["Roughness"].default_value = 0.35 if key in ("steel", "steel_dark", "brass") else 0.9
            if key in ("steel", "steel_dark", "brass"):
                bsdf.inputs["Metallic"].default_value = 0.8
            if key in ("spark", "tracer", "flash", "glass_lit"):
                try:
                    bsdf.inputs["Emission Color"].default_value = col
                    bsdf.inputs["Emission Strength"].default_value = 4.0
                except Exception:
                    pass
    return m


def get_collection(name, parent=None):
    c = bpy.data.collections.get(name)
    if not c:
        c = bpy.data.collections.new(name)
        (parent or bpy.context.scene.collection).children.link(c)
    return c


def wipe(prefixes=("WW",)):
    """Remove everything this generator made."""
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for act in list(bpy.data.actions):
        act.use_fake_user = False
        bpy.data.actions.remove(act)
    for coll in (bpy.data.meshes, bpy.data.armatures, bpy.data.actions, bpy.data.cameras, bpy.data.lights,
                 bpy.data.curves):
        for d in list(coll):
            if d.users == 0 or coll is bpy.data.actions:
                coll.remove(d)
    for c in list(bpy.data.collections):
        bpy.data.collections.remove(c)
    for m in list(bpy.data.materials):
        if m.name.startswith("WW_"):
            bpy.data.materials.remove(m)


def _se(v, p):
    return math.copysign(abs(v) ** (2.0 / p), v)


def R(c, rx, ry=None, ryb=None, w=None, p=None, mat=None, axis=None, zfn=None, rfn=None):
    """One loft ring. rx = half width, ry = half depth toward `front`, ryb = half depth away from it."""
    return dict(c=Vector(c), rx=rx, ry=rx if ry is None else ry, ryb=ryb, w=w, p=p, mat=mat, axis=axis,
                zfn=zfn, rfn=rfn)


def _swap_lr(name):
    if name.endswith("_l"):
        return name[:-2] + "_r"
    if name.endswith("_r"):
        return name[:-2] + "_l"
    return name


class MB:
    """Mesh builder. Accumulates weighted, material-tagged geometry into one object."""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.dl = self.bm.verts.layers.deform.verify()
        self.groups = []
        self.mats = []
        self.xf = None
        self.mirror = False
        self.warp = None
        self.default_w = None

    def gi(self, bone):
        if bone not in self.groups:
            self.groups.append(bone)
        return self.groups.index(bone)

    def mi(self, key):
        if key not in self.mats:
            self.mats.append(key)
        return self.mats.index(key)

    def vert(self, co, w=None):
        co = Vector(co)
        if self.xf is not None:
            co = self.xf @ co
        if self.mirror:
            co.x = -co.x
        if self.warp is not None:
            co = self.warp(co)
        v = self.bm.verts.new(co)
        w = w or self.default_w
        if w:
            tot = sum(w.values())
            for b, x in w.items():
                if x <= 0:
                    continue
                if self.mirror:
                    b = _swap_lr(b)
                v[self.dl][self.gi(b)] = x / tot
        return v

    def face(self, verts, mat):
        try:
            f = self.bm.faces.new(verts)
        except ValueError:
            return None
        f.material_index = self.mi(mat)
        return f

    def poly(self, verts, faces, mat, w=None):
        vs = [self.vert(v, w) for v in verts]
        return [self.face([vs[i] for i in f], mat) for f in faces]

    def box(self, center, size, mat, w=None, rot=None, taper=(1.0, 1.0), shift=(0.0, 0.0)):
        """Box with its top face scaled by `taper` and shifted by `shift` (local x, y)."""
        c = Vector(center)
        sx, sy, sz = (s / 2 for s in size)
        m = rot.to_matrix() if isinstance(rot, Euler) else (rot or Matrix.Identity(3))
        pts = []
        for z, (tx, ty), (ox, oy) in ((-sz, (1, 1), (0, 0)), (sz, taper, shift)):
            for x, y in ((-sx, -sy), (sx, -sy), (sx, sy), (-sx, sy)):
                pts.append(c + m @ Vector((x * tx + ox, y * ty + oy, z)))
        faces = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        return self.poly(pts, faces, mat, w)

    def loft(self, rings, mat, w=None, n=8, p=2.0, caps=(True, True), arc=None, thick=None,
             front=(0, -1, 0), matfn=None):
        """Skin a list of rings. Angle 0 points at `front`, 90 deg at the character's left (+X)."""
        front = Vector(front)
        closed = arc is None
        if closed:
            angs = [2 * pi * j / n for j in range(n)]
        else:
            a0, a1 = radians(arc[0]), radians(arc[1])
            angs = [a0 + (a1 - a0) * j / n for j in range(n + 1)]
        cs = [r["c"] for r in rings]
        vrings = []
        for i, r in enumerate(rings):
            ax = r["axis"]
            if ax is None:
                lo, hi = i - 1, i + 1
                ax = cs[min(hi, len(cs) - 1)] - cs[max(lo, 0)]
                while ax.length < 1e-6 and (lo > 0 or hi < len(cs) - 1):
                    lo, hi = lo - 1, hi + 1
                    ax = cs[min(hi, len(cs) - 1)] - cs[max(lo, 0)]
                if ax.length < 1e-6:
                    ax = Vector((0, 0, 1))
            ax = Vector(ax).normalized()
            f = front - ax * front.dot(ax)
            if f.length < 1e-3:
                f = Vector((0, 0, 1)) - ax * ax.z
            f.normalize()
            s = ax.cross(f)
            pp = r["p"] or p
            rw = r["w"] or w
            row = []
            for a in angs:
                ca, sa = cos(a), sin(a)
                ry = r["ry"] if (ca >= 0 or r["ryb"] is None) else r["ryb"]
                k = r["rfn"](a) if r["rfn"] else 1.0
                pt = r["c"] + s * (r["rx"] * k * _se(sa, pp)) + f * (ry * k * _se(ca, pp))
                if r["zfn"]:
                    pt = pt + Vector((0, 0, r["zfn"](a)))
                ww = rw(a) if callable(rw) else rw
                row.append(self.vert(pt, ww))
            vrings.append(row)
        faces = []
        m = len(angs)
        for i in range(len(rings) - 1):
            fm = rings[i]["mat"] or mat
            for j in range(m if closed else m - 1):
                j2 = (j + 1) % m
                mk = matfn(i, j) if matfn else None
                f = self.face([vrings[i][j], vrings[i][j2], vrings[i + 1][j2], vrings[i + 1][j]], mk or fm)
                if f:
                    faces.append(f)
        if closed:
            if caps[0]:
                self.face(vrings[0], rings[0]["mat"] or mat)
            if caps[1]:
                self.face(vrings[-1], rings[-2]["mat"] or mat)
        elif thick:
            self.bm.normal_update()
            bmesh.ops.solidify(self.bm, geom=faces, thickness=thick)
        return vrings

    def finish(self, collection, armature=None, smooth=False):
        bm = self.bm
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.normal_update()
        for f in bm.faces:          # faces tagged as ground must face up whatever the recalculation decided
            if f.tag and f.normal.z < 0:
                f.normal_flip()
        for f in bm.faces:
            f.smooth = smooth
        me = bpy.data.meshes.new(self.name)
        bm.to_mesh(me)
        bm.free()
        for k in self.mats:
            me.materials.append(get_mat(k))
        ob = bpy.data.objects.new(self.name, me)
        collection.objects.link(ob)
        for g in self.groups:
            ob.vertex_groups.new(name=g)
        if armature:
            ob.parent = armature
            md = ob.modifiers.new("Armature", "ARMATURE")
            md.object = armature
        return ob


# ---------------------------------------------------------------- armature

def build_armature(name, bones, collection, scale=1.0):
    """bones: list of (name, parent, head, tail)."""
    arm = bpy.data.armatures.new(name)
    ob = bpy.data.objects.new(name, arm)
    collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob
    for o in bpy.context.view_layer.objects:
        o.select_set(o is ob)
    bpy.ops.object.mode_set(mode="EDIT")
    for bn, parent, head, tail in bones:
        eb = arm.edit_bones.new(bn)
        eb.head = Vector(head) * scale
        eb.tail = Vector(tail) * scale
        eb.roll = 0.0
        if parent:
            eb.parent = arm.edit_bones[parent]
    bpy.ops.object.mode_set(mode="OBJECT")
    for pb in ob.pose.bones:
        pb.rotation_mode = "QUATERNION"
    arm.display_type = "STICK"
    ob.show_in_front = True
    return ob


# ---------------------------------------------------------------- posing

def rot(x=0, y=0, z=0, loc=None):
    """Rotation in degrees about the character's axes, relative to the parent. +X swings a hanging limb back."""
    d = dict(rel=(x, y, z))
    if loc:
        d["loc"] = loc
    return d


def aim(d, fwd=None, loc=None):
    """Point the bone along `d` (armature space). `fwd` = where the bone's rest-forward side should face."""
    o = dict(dir=d, fwd=fwd)
    if loc:
        o["loc"] = loc
    return o


def ik(target, pole, fwd=None):
    """Two-bone IK for this bone and its first child. `target` is where the child's tail should land."""
    return dict(ik=target, pole=pole, fwd=fwd)


def mirror_pose(pose):
    out = {}
    for b, s in pose.items():
        s = dict(s)
        if "rel" in s:
            x, y, z = s["rel"]
            s["rel"] = (x, -y, -z)
        for k in ("dir", "fwd", "ik", "pole", "loc"):
            if s.get(k) is not None:
                v = s[k]
                s[k] = (-v[0], v[1], v[2])
        out[_swap_lr(b)] = s
    return out


def _frame(d, f):
    y = Vector(d).normalized()
    f = Vector(f)
    f = f - y * f.dot(y)
    if f.length < 1e-4:
        f = Vector((0, 0, 1)) - y * y.z
        if f.length < 1e-4:
            f = Vector((1, 0, 0))
    f.normalize()
    x = y.cross(f)
    return Matrix((x, y, f)).transposed()


class Poser:
    def __init__(self, arm):
        self.arm = arm
        bones = arm.data.bones
        self.order = []

        def walk(b):
            self.order.append(b.name)
            for c in b.children:
                walk(c)
        for b in bones:
            if not b.parent:
                walk(b)
        self.rest = {b.name: b.matrix_local.to_3x3() for b in bones}
        self.head = {b.name: b.head_local.copy() for b in bones}
        self.tail = {b.name: b.tail_local.copy() for b in bones}
        self.parent = {b.name: (b.parent.name if b.parent else None) for b in bones}
        self.child = {b.name: (b.children[0].name if b.children else None) for b in bones}
        self.rdir = {n: (self.tail[n] - self.head[n]).normalized() for n in self.order}
        self.rfwd = {n: (Vector((0, 0, 1)) if abs(self.rdir[n].y) > 0.9 else Vector((0, -1, 0))) for n in self.order}

    def _dir_delta(self, n, d, fwd):
        d = Vector(d).normalized()
        if fwd is None:
            return self.rdir[n].rotation_difference(d).to_matrix()
        return _frame(d, fwd) @ _frame(self.rdir[n], self.rfwd[n]).transposed()

    def solve(self, pose):
        """Returns {bone: (quat, loc)} plus posed head positions."""
        D, P, out = {}, {}, {}
        pose = dict(pose)
        I = Matrix.Identity(3)
        for n in self.order:
            par = self.parent[n]
            Dp = D[par] if par else I
            spec = pose.get(n)
            loc = Vector(spec["loc"]) if spec and spec.get("loc") else Vector((0, 0, 0))
            if par:
                P[n] = P[par] + Dp @ (self.head[n] - self.head[par]) + Dp @ loc
            else:
                P[n] = self.head[n] + loc
            if spec is None:
                Db = Dp
            elif "ik" in spec:
                c = self.child[n]
                a = (self.head[c] - self.head[n]).length
                b = (self.tail[c] - self.head[c]).length
                t = Vector(spec["ik"]) - P[n]
                d = max(min(t.length, (a + b) * 0.999), abs(a - b) + 1e-4)
                td = t.normalized()
                pole = Vector(spec["pole"])
                pole = pole - td * pole.dot(td)
                pole.normalize()
                ca = (a * a + d * d - b * b) / (2 * a * d)
                sa = math.sqrt(max(0.0, 1 - ca * ca))
                elbow = P[n] + td * (a * ca) + pole * (a * sa)
                fwd = spec.get("fwd")
                Db = self._dir_delta(n, elbow - P[n], fwd)
                pose[c] = dict(dir=(P[n] + td * d) - elbow, fwd=fwd)
            elif "dir" in spec:
                Db = self._dir_delta(n, spec["dir"], spec.get("fwd"))
            else:
                x, y, z = (radians(v) for v in spec["rel"])
                Db = Dp @ Euler((x, y, z), "XYZ").to_matrix()
            D[n] = Db
            Rb = self.rest[n]
            B = Rb.transposed() @ Dp.transposed() @ Db @ Rb
            out[n] = (B.to_quaternion(), Rb.transposed() @ Dp.transposed() @ loc)
        return out

    def apply(self, pose):
        res = self.solve(pose)
        for n, (q, l) in res.items():
            pb = self.arm.pose.bones[n]
            pb.rotation_quaternion = q
            pb.location = l
        return res


def make_action(arm, poser, name, keys, loop=False):
    """keys: list of (frame, pose). Keys every bone so actions never inherit a stale pose."""
    old = bpy.data.actions.get(name)
    if old:
        bpy.data.actions.remove(old)
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    ad = arm.animation_data_create()
    ad.action = act
    loc_bones = set()
    for _, pose in keys:
        loc_bones |= {b for b, s in pose.items() if s.get("loc")}
    if loop and keys[-1][1] is not keys[0][1]:
        pass
    prev = {}
    for frame, pose in keys:
        res = poser.apply(pose)
        for n, (q, l) in res.items():
            pb = arm.pose.bones[n]
            if n in prev and prev[n].dot(q) < 0:
                q = -q
                pb.rotation_quaternion = q
            prev[n] = q.copy()
            pb.keyframe_insert("rotation_quaternion", frame=frame)
            if n in loc_bones:
                pb.keyframe_insert("location", frame=frame)
    act.frame_range = (keys[0][0], keys[-1][0])
    try:
        act.use_frame_range = True
        act.use_cyclic = bool(loop)
    except Exception:
        pass
    return act


def set_action(arm, act):
    ad = arm.animation_data_create()
    ad.action = act
    try:
        if act and act.slots and ad.action_slot is None:
            ad.action_slot = act.slots[0]
    except Exception:
        pass


def lerp(a, b, t):
    return a + (b - a) * t


def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def bell(t):
    """0 -> 1 -> 0 over t in [0,1]."""
    return sin(pi * max(0.0, min(1.0, t))) ** 2


def cyl(mb, a, b, r, mat, n=8, r2=None, w=None, caps=(True, True), p=2.0, front=(0, -1, 0)):
    """Cylinder / cone between two points."""
    return mb.loft([R(a, r, w=w), R(b, r if r2 is None else r2, w=w)], mat, n=n, caps=caps, p=p, front=front)


def gable(mb, center, size, rise, mat, w=None, overhang=0.0, ridge_along="x", end_mat=None):
    """Gabled roof. `size` = (x, y) footprint, base at center z, ridge `rise` above it."""
    cx, cy, cz = center
    sx, sy = size[0] / 2 + overhang, size[1] / 2 + overhang
    if ridge_along == "x":
        v = [(cx - sx, cy - sy, cz), (cx + sx, cy - sy, cz), (cx + sx, cy + sy, cz), (cx - sx, cy + sy, cz),
             (cx - sx, cy, cz + rise), (cx + sx, cy, cz + rise)]
        roof, ends = [(0, 1, 5, 4), (2, 3, 4, 5)], [(0, 4, 3), (1, 2, 5)]
    else:
        v = [(cx - sx, cy - sy, cz), (cx + sx, cy - sy, cz), (cx + sx, cy + sy, cz), (cx - sx, cy + sy, cz),
             (cx, cy - sy, cz + rise), (cx, cy + sy, cz + rise)]
        roof, ends = [(1, 2, 5, 4), (3, 0, 4, 5)], [(0, 1, 4), (2, 3, 5)]
    vs = [mb.vert(p, w) for p in v]
    for f in roof:
        mb.face([vs[i] for i in f], mat)
    for f in ends:
        mb.face([vs[i] for i in f], end_mat or mat)
    mb.face([vs[0], vs[1], vs[2], vs[3]], end_mat or mat)


def static(name, fn, collection, location=(0, 0, 0), **kw):
    mb = MB(name)
    fn(mb, **kw)
    ob = mb.finish(collection)
    ob.location = location
    return ob
