"""Everything read from the body model: scale, chest anchor, lean, duck, speed.

Lean and duck come from the shoulders first. While aiming, the gun hand sits right
in front of the face from the camera's point of view, so face landmarks are the
first thing to get occluded, and shoulders almost never are. The head adds to that
when it can be seen (see head_weight in config.py).
"""
import math

import numpy as np

from hand_features import iso2d
from one_euro import OneEuro
from windows import TimedWindow

L_SHOULDER, R_SHOULDER = 11, 12
L_WRIST, R_WRIST = 15, 16
HEAD = (0, 2, 5, 7, 8)      # nose, eyes, ears
CHAINS = {"L": (L_SHOULDER, L_WRIST), "R": (R_SHOULDER, R_WRIST)}


def _deadzone(value, zone):
    if abs(value) <= zone:
        return 0.0
    return math.copysign((abs(value) - zone) / (1.0 - zone), value)


class BodyTracker:
    def __init__(self, cfg):
        self.cfg = cfg
        self.anchor_filter = OneEuro(cfg.anchor_min_cutoff, cfg.anchor_beta)
        self.lean_filter = OneEuro(cfg.lean_min_cutoff, cfg.lean_beta)
        self.duck_filter = OneEuro(cfg.lean_min_cutoff, cfg.lean_beta)
        self.sw = None              # shoulder width, H (robust, see update)
        self.sw_window = TimedWindow(cfg.sw_window_s)
        self.anchor = None          # filtered mid-shoulder point, H
        self.anchor_raw = None
        self.neutral_x = None       # chest X that means lean = 0
        self.stand_y = None         # chest Y that means duck = 0
        self.lean = 0.0
        self.duck = 0.0
        self.speed = 0.0
        self.visible = False        # body seen this frame
        self.last_seen_t = -1e9
        self.last_t = None
        self.pts = None             # this frame's pose points in H units
        self.vis = None
        self._recenter = False
        self.baseline_set = False
        self.still_ref = None
        self.still_since = 0.0
        self.head_w = 0.0           # 0..1, how much the head is trusted right now
        self.head_rel0 = None       # where the head sits relative to the chest when standing neutral, sw

    def recenter(self):
        self._recenter = True

    def tracking(self, t):
        return t - self.last_seen_t <= self.cfg.tracking_hold_s

    def scale(self):
        """Image heights per real metre at the chest's depth."""
        return self.sw / self.cfg.shoulder_width_m

    def holster_y(self):
        return self.anchor[1] + self.cfg.holster_below_shoulder * self.sw

    def wrist(self, chain, min_vis=0.5):
        if not self.visible or self.vis[CHAINS[chain][1]] < min_vis:
            return None
        return self.pts[CHAINS[chain][1]]

    def shoulder(self, chain):
        return self.pts[CHAINS[chain][0]]

    def update(self, t, pose, aspect):
        cfg = self.cfg
        dt = 0.0 if self.last_t is None else max(1e-3, t - self.last_t)
        self.last_t = t
        self.visible = pose is not None and min(pose.vis[L_SHOULDER], pose.vis[R_SHOULDER]) > 0.5
        if self.visible:
            # The body model will invent a confident torso from a pair of hands. Real
            # shoulders are level-ish, or (turned sideways) close together. Never stacked.
            d = np.abs((pose.pts[L_SHOULDER, :2] - pose.pts[R_SHOULDER, :2]) * np.array([aspect, 1.0]))
            self.visible = bool(d[1] <= 1.2 * d[0] or (self.sw is not None and d[1] < 0.5 * self.sw))

        if not self.visible:
            if t - self.last_seen_t > cfg.body_hold_s:
                decay = math.exp(-dt / 0.3)
                self.lean *= decay
                self.duck *= decay
                self.speed *= decay
            return

        if t - self.last_seen_t > cfg.baseline_forget_s:
            self.baseline_set = False           # player walked off, the next one is a new person
            self.sw_window.clear()
        self.last_seen_t = t
        self.pts = iso2d(pose.pts, aspect)
        self.vis = pose.vis

        # Scale. Turning sideways shrinks the visible shoulder width, so take a high
        # percentile over a few seconds: a brief turn is ignored, and leaning in to the
        # keyboard wears off in seconds (a slow-decay envelope kept it wrong for 20 s).
        self.sw_window.push(t, float(np.linalg.norm(self.pts[L_SHOULDER] - self.pts[R_SHOULDER])))
        self.sw = max(self.sw_window.percentile(cfg.sw_percentile), 0.05)

        # The aim uses the raw anchor: hand and chest come from the same frame, so their
        # difference is steady in a dodge, while a separately filtered chest would lag the
        # hand and swing the crosshair. The filtered one is for slow things like the holster line.
        prev = self.anchor_raw
        self.anchor_raw = 0.5 * (self.pts[L_SHOULDER] + self.pts[R_SHOULDER])
        self.anchor = self.anchor_filter(self.anchor_raw, t)
        chest = self.anchor_raw

        if prev is not None:
            v = float(np.linalg.norm(chest - prev)) / dt / self.sw
            self.speed += (min(1.0, v / cfg.speed_full) - self.speed) * min(1.0, dt / 0.15)

        # Baseline: where this player stands when not dodging. Until it is known the
        # baseline follows the player (lean = duck = 0). It locks in the first time they
        # stand still, or immediately on a recenter from the game.
        if self._recenter:
            self.baseline_set, self._recenter = True, False
            self.neutral_x, self.stand_y = float(chest[0]), float(chest[1])
            self.head_rel0 = None
        elif not self.baseline_set:
            self.neutral_x, self.stand_y = float(chest[0]), float(chest[1])
            if self.still_ref is None or np.linalg.norm(chest - self.still_ref) > cfg.settle_radius * self.sw:
                self.still_ref, self.still_since = chest.copy(), t
            elif t - self.still_since >= cfg.settle_s:
                self.baseline_set, self.still_ref = True, None

        # The head's own extra travel, in shoulder widths, against where it sits when standing neutral.
        seen = [i for i in HEAD if self.vis[i] >= cfg.head_min_vis]
        extra = np.zeros(2)
        want_w = 0.0
        if len(seen) >= 2:
            rel = (self.pts[seen].mean(axis=0) - chest) / self.sw
            if self.head_rel0 is None or not self.baseline_set:
                self.head_rel0 = rel.copy()
            extra = rel - self.head_rel0
            want_w = 1.0
        self.head_w += (want_w - self.head_w) * min(1.0, dt / cfg.head_fade_s)
        extra *= np.array([cfg.head_weight, cfg.head_weight_duck]) * self.head_w

        raw_lean = (chest[0] - self.neutral_x + extra[0] * self.sw) / (cfg.lean_full * self.sw)
        raw_duck = (chest[1] - self.stand_y + extra[1] * self.sw) / (cfg.duck_full * self.sw)
        lean = float(np.clip(self.lean_filter([raw_lean], t)[0], -1.0, 1.0))
        duck = float(np.clip(self.duck_filter([raw_duck], t)[0], 0.0, 1.0))
        self.lean = _deadzone(lean, cfg.lean_deadzone)
        self.duck = _deadzone(duck, cfg.duck_deadzone)

        # Baselines follow the player slowly so nobody ends up permanently leaning.
        if len(seen) >= 2 and cfg.neutral_drift_s > 0 and abs(raw_lean) < 0.35 and raw_duck < 0.25:
            self.head_rel0 += (rel - self.head_rel0) * min(1.0, dt / cfg.neutral_drift_s)
        if cfg.neutral_drift_s > 0 and abs(raw_lean) < 0.35:
            self.neutral_x += (chest[0] - self.neutral_x) * min(1.0, dt / cfg.neutral_drift_s)
        if chest[1] < self.stand_y:
            self.stand_y += (chest[1] - self.stand_y) * min(1.0, dt / cfg.stand_rise_s)
        elif raw_duck < 0.25:
            self.stand_y += (chest[1] - self.stand_y) * min(1.0, dt / cfg.stand_relax_s)
