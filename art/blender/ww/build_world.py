"""Build the world kit (train, track, terrain, town, FX) plus a diorama, and export it.

Layout: the diorama sits around the origin with the line along Y. The asset library is laid out in rows at x > 400.
"""
import os
import random
from math import pi, radians

import bpy
from mathutils import Vector

from . import anims, core, fx, horse, human, props, scenery, town, train, variants
from .build_all import export_rig, export_static

LIB_X = 420.0


def _static(name, fn, kw, coll, loc=(0, 0, 0), rotz=0.0):
    mb = core.MB(name)
    fn(mb, **kw)
    ob = mb.finish(coll)
    ob.location = loc
    ob.rotation_euler = (0, 0, rotz)
    return ob


def _instance(src, coll, loc, rotz=0.0, scale=1.0, name=None):
    ob = bpy.data.objects.new(name or (src.name + "_i"), src.data)
    coll.objects.link(ob)
    ob.location = loc
    ob.rotation_euler = (0, 0, rotz)
    ob.scale = (scale, scale, scale)
    return ob


def build_library():
    """One of everything, in rows. These are the objects that get exported."""
    lib = {}
    rows = [("WW_Track", scenery.TRACK, 0, 9), ("WW_Scatter", scenery.SCATTER, 30, 5), ("WW_Landmarks", scenery.LANDMARKS, 120, 95),
            ("WW_Props", town.PROPS, -40, 9), ("WW_Buildings", town.BUILDINGS, -90, 24), ("WW_FX", fx.FX, -120, 2.5)]
    for cname, table, y, step in rows:
        coll = core.get_collection(cname)
        x = LIB_X
        for name, (fn, kw) in table.items():
            lib[name] = _static(name, fn, kw, coll, loc=(x, y, 0))
            x += step
    coll = core.get_collection("WW_Terrain")
    for k, seed in enumerate((1, 2, 3)):
        nm = "TerrainTile_" + "ABC"[k]
        lib[nm] = _static(nm, scenery.terrain_tile, dict(seed=seed), coll, loc=(LIB_X + 150 + k * (scenery.TILE_W + 20), 300, 0))
    x = LIB_X
    for name, fn in town.RIGGED.items():
        arm, ob = fn()
        arm.location = (x, -160, 0)
        lib[name] = arm
        x += 22
    arm, ob = fx.player_revolver()
    arm.location = (LIB_X, -130, 1.0)
    lib["PlayerRevolver"] = arm
    x = LIB_X
    for kind in train.BUILDERS:
        for lv in ("player", "bandit"):
            if lv == "bandit" and kind in ("Tender", "Flatcar_Logs"):
                continue
            arm, ob = train.build_vehicle(kind, lv, location=(x, -220, scenery.RAIL_TOP))
            lib[arm.name] = arm
            x += 6
    g, _ = props.build_gatling("Gatling", location=(LIB_X - 10, -130, 0))
    lib["Gatling"] = g
    return lib


def build_diorama(lib):
    dio = core.get_collection("WW_Diorama")
    rng = random.Random(11)
    TL = scenery.TILE_LEN
    tiles = ["TerrainTile_A", "TerrainTile_B", "TerrainTile_C"]
    seeds = {"TerrainTile_A": 1, "TerrainTile_B": 2, "TerrainTile_C": 3}
    y0 = -TL * 1.5
    for k, t in enumerate(tiles):
        _instance(lib[t], dio, (0, y0 + k * TL, 0))
        for j in range(3):
            y = y0 + k * TL + j * scenery.TRACK_LEN
            _instance(lib["Track_Straight_20m"], dio, (0, y, 0))
            _instance(lib["Track_Straight_20m"], dio, (scenery.SECOND_TRACK_X, y, 0))
        for j in range(2):
            _instance(lib["TelegraphSpan_30m"], dio, (13.0, y0 + k * TL + j * 30, 0))
        # scatter
        names = list(scenery.SCATTER)
        for i in range(70):
            nm = rng.choice(names)
            x = rng.uniform(-110, 110)
            y = rng.uniform(0, TL)
            if -18 < x < scenery.SECOND_TRACK_X + 14 or nm in ("Fence", "WagonWheel"):
                continue
            if y0 + k * TL + y > 40 and -70 < x < -8:     # keep the town clear
                continue
            z = scenery._h(x, y, seeds[t])
            _instance(lib[nm], dio, (x, y0 + k * TL + y, z - 0.05), rotz=rng.uniform(0, 6.28), scale=rng.uniform(0.8, 1.3))
    for nm, x, y, s in (("Mesa_A", -105, -40, 1.0), ("Mesa_B", 112, 10, 1.0), ("Mesa_C", 95, -75, 1.0), ("Butte_A", -95, 55, 1.0), ("Butte_B", 88, 70, 1.0),
                        ("RockArch", 70, -20, 1.0), ("Mesa_C", -118, 20, 1.3)):
        _instance(lib[nm], dio, (x, y, 4.0), rotz=rng.uniform(0, 6.28), scale=s)
    # trains (fresh rigs so the library ones stay at rest)
    rt = scenery.RAIL_TOP
    player = train.build_consist(["Locomotive", "Tender", "PassengerCar", "Boxcar", "Flatcar_Crates", "Caboose"], "player", 0.0, -52.0, rt, prefix="Dio_Player")
    bandit = train.build_consist(["Locomotive", "Tender", "Flatcar_Gatling", "PassengerCar", "Boxcar"], "bandit", scenery.SECOND_TRACK_X, -30.0, rt, prefix="Dio_Bandit")
    for arm, _ in player + bandit:
        core.set_action(arm, bpy.data.actions[arm.name + "_roll"])
    # station and town on the west side, facing the line
    _instance(lib["Station"].data and lib["Station"], dio, (-9.6, 62, 0), rotz=pi / 2)
    for nm, y in (("GeneralStore", 40), ("Sheriff", 52), ("Bank", 62), ("Hotel", 74), ("Church", 88)):
        _instance(lib[nm], dio, (-46, y, scenery._h(-46, y % TL, 3)), rotz=pi / 2)
    for nm, x, y, r in (("Barn", -66, 60, pi / 2), ("Shack", -60, 84, 0.4), ("Outhouse", -58, 44, 2.0), ("Wagon", -34, 50, 0.5), ("Trough", -40, 47, pi / 2),
                        ("HitchingPost", -40.5, 57, pi / 2), ("WantedBoard", -14.5, 50, pi / 2), ("Signpost", -14, 78, 0),  ("HayBale", -62, 52, 0.3), ("Track_BufferStop", 0, 0, 0)):
        if nm == "Track_BufferStop":
            continue
        _instance(lib[nm], dio, (x, y, 0), rotz=r)
    for ctor, x, y, r in ((town.saloon, -46, 26, pi / 2), (town.water_tower, 5.5 - 11.0, 30, pi), (town.windmill, -70, 36, 0.6), (town.station_bell, -4.2, 69, pi / 2)):
        arm, _ = ctor()
        base = arm.name.split(".")[0]
        arm.name = "Dio_" + base
        arm.location = (x, y, 1.0 if base == "StationBell" else 0)
        arm.rotation_euler = (0, 0, r)
        act = {"Saloon": "Saloon_doors_swing", "WaterTower": "WaterTower_spout_swing", "Windmill": "Windmill_spin", "StationBell": "StationBell_ring"}[base]
        core.set_action(arm, bpy.data.actions[act])
    _instance(lib["BottleCrate"], dio, (-4.0, 55, 1.0), rotz=pi / 2)
    _instance(lib["StartSign"], dio, (-16, 40, 0), rotz=pi / 2)
    _instance(lib["LowGantry"], dio, (0, -70, 0))
    _instance(lib["TunnelPortal"], dio, (0, -88, 0))
    # cast
    first = None
    acts = None

    def actor(kind, name, loc, rotz, action):
        nonlocal first, acts
        arm, _ = human.build_human(name, variants.VARIANTS[kind])
        if acts is None:
            acts = anims.build_human_actions(arm)
        arm.location = loc
        arm.rotation_euler = (0, 0, rotz)
        core.set_action(arm, acts[action])
        return arm
    roof = rt + 3.97
    actor("Bandit", "Dio_Bandit_Roof", (0, 12.0, rt + 3.91), 0.0, "aim")
    actor("Gunslinger", "Dio_Gunslinger_Roof", (0.3, 14.5, rt + 3.91), 0.2, "dual_aim")
    actor("Dynamiter", "Dio_Dynamiter", (scenery.SECOND_TRACK_X, 10.0, rt + 3.91), -pi / 2, "throw")
    gy = bandit[2][0].location.y
    gx = scenery.SECOND_TRACK_X + 0.55
    hv = actor("Heavy", "Dio_Heavy", (gx, gy, rt + train.FLOOR + 0.1), -pi / 2, "gatling_fire")
    g, _ = props.build_gatling("Dio_Gatling", location=(gx - 0.88 * 1.1, gy, rt + train.FLOOR + 0.1))
    g.rotation_euler = (0, 0, -pi / 2)
    core.set_action(g, bpy.data.actions["gatling_spin"])
    hacts = None
    for i, (kind, hname, x, y, act) in enumerate((("Bandit", "Horse_Bay", -9.0, -8.0, "ride_aim_left"), ("Rifleman", "Horse_Black", -12.0, 6.0, "ride_gallop"),
                                                  ("Deputy", "Horse_Palomino", -8.0, 20.0, "ride_aim_left"))):
        h, _ = horse.build_horse(f"Dio_Horse_{i}", variants.HORSES[hname], location=(x, y, 0))
        if hacts is None:
            hacts = horse.build_horse_actions(h)
        core.set_action(h, hacts["gallop"])
        r = actor(kind, f"Dio_Rider_{i}", (0, 0, 0), 0.0, act)
        bpy.context.view_layer.update()
        horse.mount(r, h, scale=variants.VARIANTS[kind].get("scale", 1.0))
    return dio


def build():
    core.wipe()
    sc = bpy.context.scene
    sc.render.fps = core.FPS
    sc.frame_start, sc.frame_end = 0, 48
    lib = build_library()
    build_diorama(lib)
    return lib


def export_all(outdir, lib=None):
    names = {}
    for sub in ("train", "track", "terrain", "scenery", "buildings", "props", "fx", "setpieces"):
        os.makedirs(os.path.join(outdir, sub), exist_ok=True)
    folders = [(scenery.TRACK, "track"), (scenery.SCATTER, "scenery"), (scenery.LANDMARKS, "scenery"), (town.PROPS, "props"), (town.BUILDINGS, "buildings"),
               (fx.FX, "fx")]
    done = []
    for table, sub in folders:
        for nm in table:
            export_static(bpy.data.objects[nm], os.path.join(outdir, sub, "SM_" + nm))
            done.append(nm)
    for nm in ("TerrainTile_A", "TerrainTile_B", "TerrainTile_C"):
        export_static(bpy.data.objects[nm], os.path.join(outdir, "terrain", "SM_" + nm))
        done.append(nm)

    def acts(prefix):
        return [a for a in bpy.data.actions if a.name.startswith(prefix)]
    for nm in town.RIGGED:
        export_rig(bpy.data.objects[nm], os.path.join(outdir, "setpieces", "SK_" + nm), acts(nm + "_"))
        done.append(nm)
    export_rig(bpy.data.objects["PlayerRevolver"], os.path.join(outdir, "fx", "SK_PlayerRevolver"), acts("fp_"))
    for ob in bpy.data.objects:
        if ob.type == "ARMATURE" and ob.name.startswith("Train_"):
            export_rig(ob, os.path.join(outdir, "train", "SK_" + ob.name),
                       [x for x in bpy.data.actions if x.name in (ob.name + "_roll", ob.name + "_doors_open")])
            done.append(ob.name)
    return done
