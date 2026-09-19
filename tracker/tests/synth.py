"""Synthetic landmarks for testing the pipeline with no camera.

The hand is a rough 3D model (metres) seen from the side: index along +x, thumb
up. Real use has the finger pointing at the camera, which these cannot mimic,
so the tests check logic, not real-world thresholds.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from config import Config  # noqa: E402
from landmarks import Frame, Hand, Pose  # noqa: E402
from pipeline import Pipeline  # noqa: E402

ASPECT = 16 / 9
SW = 0.27                   # shoulder width in image heights (player about 1.5 m away)
M = SW / 0.38               # image heights per metre
SHOULDER_Y = 0.45


def _lerp(a, b, k):
    return np.array(a) + (np.array(b) - np.array(a)) * k


def hand_model(thumb=0.0, open_palm=False):
    """21 points in metres. thumb: 0 = cocked (up), 1 = hammer down."""
    p = np.zeros((21, 3))
    if open_palm:
        p[1:5] = [(0.02, 0.03, -0.01), (0.04, 0.06, -0.01), (0.055, 0.09, -0.01), (0.07, 0.115, -0.01)]
        for i, (mcp_y, z) in enumerate([(0.02, 0.0), (0.0, 0.0), (-0.02, 0.0), (-0.04, 0.0)]):
            base = 5 + 4 * i
            for j, x in enumerate((0.09, 0.13, 0.155, 0.18)):
                p[base + j] = (x, mcp_y * (1 + j * 0.3), z)
        return p
    p[1] = (0.025, 0.015, -0.01)
    p[2] = (0.055, 0.04, -0.015)
    p[3] = _lerp((0.065, 0.07, -0.015), (0.085, 0.05, -0.01), thumb)
    p[4] = _lerp((0.07, 0.095, -0.015), (0.115, 0.04, -0.005), thumb)
    p[5:9] = [(0.09, 0.02, 0), (0.135, 0.02, 0), (0.16, 0.02, 0), (0.185, 0.02, 0)]
    for i, z in enumerate((0.02, 0.04, 0.055)):
        base = 9 + 4 * i
        mx = 0.09 - 0.007 * i
        p[base:base + 4] = [(mx, 0.0, z), (mx + 0.035, -0.01, z), (mx + 0.025, -0.035, z), (mx + 0.005, -0.03, z)]
    return p


def make_hand(wrist_xy, thumb=0.0, pitch=0.0, open_palm=False):
    """wrist_xy in image heights. pitch in radians, positive tips the finger up."""
    p = hand_model(thumb, open_palm)
    c, s = np.cos(pitch), np.sin(pitch)
    rot = p.copy()
    rot[:, 0] = p[:, 0] * c - p[:, 1] * s
    rot[:, 1] = p[:, 0] * s + p[:, 1] * c
    pts = np.zeros((21, 3))
    pts[:, 0] = (wrist_xy[0] + rot[:, 0] * M) / ASPECT
    pts[:, 1] = wrist_xy[1] - rot[:, 1] * M
    pts[:, 2] = rot[:, 2] * M / ASPECT
    return Hand(pts, rot - rot.mean(axis=0), "Right", 0.95)


def make_pose(center_x=0.5 * ASPECT, drop=0.0, wrists=None):
    """center_x and wrists in image heights. drop lowers the whole upper body (ducking)."""
    pts = np.zeros((33, 3))
    vis = np.full(33, 0.95)
    sy = SHOULDER_Y + drop
    head = np.array([center_x, sy - 0.62 * SW])
    for i, (dx, dy) in {0: (0, 0), 2: (-0.08, -0.05), 5: (0.08, -0.05), 7: (-0.17, -0.02), 8: (0.17, -0.02)}.items():
        pts[i, :2] = head + np.array([dx, dy]) * SW
    pts[11, :2] = (center_x - SW / 2, sy)
    pts[12, :2] = (center_x + SW / 2, sy)
    pts[23, :2] = (center_x - SW * 0.35, sy + 1.45 * SW)
    pts[24, :2] = (center_x + SW * 0.35, sy + 1.45 * SW)
    hanging = {"L": (center_x - SW * 0.6, sy + 1.6 * SW), "R": (center_x + SW * 0.6, sy + 1.6 * SW)}
    hanging.update(wrists or {})
    pts[15, :2] = hanging["L"]
    pts[16, :2] = hanging["R"]
    pts[:, 0] /= ASPECT
    return Pose(pts, vis)


def rest_wrist(center_x=0.5 * ASPECT, drop=0.0):
    """Wrist position that puts the uncalibrated crosshair near the screen centre (right hand)."""
    return np.array([center_x + 0.25 * SW, SHOULDER_Y + drop - 0.15 * SW])


class Sim:
    def __init__(self, cfg=None, fps=30, noise=0.0, seed=1):
        self.cfg = cfg or Config()
        self.pipeline = Pipeline(self.cfg)
        self.dt = 1.0 / fps
        self.t = 0.0
        self.noise = noise
        self.rng = np.random.default_rng(seed)
        self.events = []        # (t, event)
        self.state = None

    def _jitter(self, a):
        return a + self.rng.normal(0, self.noise, a.shape) if self.noise else a

    def step(self, hands, pose):
        for h in hands:
            h.pts = self._jitter(h.pts)
            h.world = self._jitter(h.world)
        if pose is not None:
            pose.pts = self._jitter(pose.pts)
        self.state, events = self.pipeline.update(Frame(self.t, ASPECT, hands, pose))
        self.events += [(self.t, e) for e in events]
        self.t += self.dt
        return self.state

    def run(self, seconds, fn):
        """fn(k) -> (hands, pose), k goes 0..1 across the span."""
        n = max(1, round(seconds / self.dt))
        for i in range(n):
            self.step(*fn((i + 1) / n))
        return self.state

    def count(self, kind):
        return sum(1 for _, e in self.events if e[0] == kind)
