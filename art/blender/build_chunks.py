"""Headless build of the chunk set:  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python art/blender/build_chunks.py"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bpy  # noqa: E402

import ww.chunks as ch  # noqa: E402
import ww.preview as pv  # noqa: E402

ART = os.path.dirname(HERE)
metas, entries = ch.build()
gaps = ch.check_seams(metas)
print("worst height gap per joint type (m):", gaps)
assert max(gaps.values()) < 1e-6, gaps
ch.export_all(os.path.join(ART, "export"), metas)

P = os.path.join(ART, "previews") + os.sep
bpy.context.scene.frame_set(6)
xs, ys = [e[0] for e in entries], [e[1] for e in entries]
pv.shot(P + "chunks_run_top.png", target=((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, 0), dist=3000, yaw=0, pitch=89.9,
        ortho=max(xs) - min(xs) + 260, res=(2400, 900))


def at(name, png, dist=150, yaw=-60, pitch=18, ahead=25):
    x, y, a = entries[ch.DEMO_ORDER.index("FG_Chunk_" + name)]
    pv.shot(P + png, target=(x + ahead * math.cos(a), y + ahead * math.sin(a), 3), dist=dist, yaw=yaw, pitch=pitch, res=(1600, 850))


at("CurveL_A", "chunks_curve.png", dist=170, yaw=-75, pitch=22)
at("SideTrack_Start_A", "chunks_side_track.png", yaw=-70, pitch=25, ahead=60)
at("CanyonDeep_CurveL_A", "chunks_canyon.png", dist=160, yaw=-88, pitch=28)
at("Tunnel_Entry_A", "chunks_tunnel.png", dist=110, yaw=-100, pitch=12, ahead=0)
at("Gulch_A", "chunks_gulch.png", dist=120, yaw=-50, pitch=16)
pv.shot(P + "chunks_guide_test.png", target=(150, -700, 0), dist=330, yaw=-20, pitch=35, res=(1800, 800))
for ob in bpy.data.objects:
    ob.hide_render = False
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "wildwest_chunks.blend"))
