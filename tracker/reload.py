"""Reload = slap the bottom of the gun hand with the other hand.

Two overlapping hands break the hand model (one of them usually vanishes right
before contact), so contact is inferred three ways:
  A. both hands tracked, off hand close, closing fast, coming from below
  B. it was closing fast from below, then a hand vanished
  C. the body model's two wrists come together (it survives the overlap)
"""
from collections import deque

import numpy as np

from trigger import value_at


class ReloadDetector:
    def __init__(self, cfg):
        self.cfg = cfg
        self.hand_hist = deque()
        self.pose_hist = deque()
        self.last_seen = None        # (t, distance, approach, from_below)
        self.ready = True
        self.last_reload_t = -1e9
        self.suppress_until = -1e9
        self.d = None                # exposed for the debug view

    def fire_suppressed(self, t):
        return t < self.suppress_until

    def _push(self, hist, t, d):
        hist.append((t, d))
        while hist[0][0] < t - 0.6:
            hist.popleft()
        before = value_at(hist, t - self.cfg.slap_window_s)
        return 0.0 if before is None else before - d

    def update(self, t, gun_wrist, off_center, pose_gun_wrist, pose_off_wrist, sw):
        """Points are in H units (or None). Returns True on the frame a reload happens."""
        cfg = self.cfg
        hit = False
        far = False

        if gun_wrist is not None and off_center is not None:
            d = float(np.linalg.norm(off_center - gun_wrist)) / sw
            approach = self._push(self.hand_hist, t, d)
            below = off_center[1] > gun_wrist[1] - cfg.slap_above_tol * sw
            self.d = d
            self.last_seen = (t, d, approach, below)
            if d < cfg.slap_watch_radius and approach > cfg.slap_approach * 0.6:
                self.suppress_until = max(self.suppress_until, t + cfg.slap_suppress_s)
            hit = d < cfg.slap_near and approach > cfg.slap_approach and below
            far = d > cfg.slap_rearm
        else:
            self.d = None
            self.hand_hist.clear()
            if self.last_seen is not None:
                seen_t, d, approach, below = self.last_seen
                if t - seen_t > cfg.slap_vanish_s:
                    self.last_seen = None
                elif d < cfg.slap_watch_radius and approach > cfg.slap_approach and below:
                    hit = True
                    self.last_seen = None

        if pose_gun_wrist is not None and pose_off_wrist is not None:
            dw = float(np.linalg.norm(pose_off_wrist - pose_gun_wrist)) / sw
            approach = self._push(self.pose_hist, t, dw)
            below = pose_off_wrist[1] > pose_gun_wrist[1] - cfg.slap_above_tol * sw
            hit = hit or (dw < cfg.slap_pose_near and approach > cfg.slap_approach and below)
            far = far or dw > cfg.slap_rearm
        else:
            self.pose_hist.clear()

        since = t - self.last_reload_t
        if not self.ready and (far or since > cfg.slap_lockout_s):
            self.ready = True
        if hit and self.ready and since > cfg.slap_cooldown_s:
            self.ready = False
            self.last_reload_t = t
            self.suppress_until = max(self.suppress_until, t + cfg.slap_suppress_s)
            return True
        return False
