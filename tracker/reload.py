"""Reload = slap the bottom of the gun hand with the other hand.

What a slap looks like to the camera, from recorded sessions: a hand gets KICKED UP,
exactly like a recoil, while the two hands are together. Which hand the camera sees
varies: often the gun hand has dropped below the frame and it is the slapping hand
that is tracked. So the rule is symmetric: a kick from EITHER hand while the hands
are together is a reload, and a kick from the gun hand with the other hand out of
the way is a shot. One detector, one event, so a slap can never also fire.
A fast approach seen by the hand model counts too, for slaps too soft to kick.
"""
import numpy as np

from windows import TimedWindow


class ReloadDetector:
    def __init__(self, cfg):
        self.cfg = cfg
        self.rel = TimedWindow(0.25)         # recent off-hand position relative to the gun wrist, in sw
        self.dist = TimedWindow(0.6)         # hand-model distance between the hands, metres
        self.last_seen = None                # (t, distance, approach, from_below)
        self.ready = True
        self.last_reload_t = -1e9
        self.suppress_until = -1e9
        self.d = None                        # exposed for the debug view
        self.relation = None

    def fire_suppressed(self, t):
        return t < self.suppress_until

    def observe(self, t, relation):
        """relation = (dx, dy) between the two hands in shoulder widths, or None if unknown."""
        self.relation = relation
        if relation is not None:
            self.rel.push(t, relation)

    def _recent(self, t):
        pts = [v for ts, v in self.rel.buf if ts >= t - 0.25]
        return np.median(np.array(pts), axis=0) if pts else None

    def together(self, t):
        r, cfg = self._recent(t), self.cfg
        return r is not None and abs(r[0]) < cfg.slap_together_dx and abs(r[1]) < cfg.slap_together_dy

    def maybe_together(self, t):
        r, cfg = self._recent(t), self.cfg
        return r is not None and abs(r[0]) < cfg.slap_maybe_dx

    def mark_reload(self, t):
        self.ready = False
        self.last_reload_t = t
        self.suppress_until = max(self.suppress_until, t + self.cfg.slap_suppress_s)

    def can_reload(self, t):
        if not self.ready and t - self.last_reload_t > self.cfg.slap_cooldown_s:
            self.ready = True
        return self.ready

    def approach(self, t, gun_wrist, off_center, scale, sw):
        """Both hands tracked by the hand model (points in H, scale in H/m). True = slap by approach."""
        cfg = self.cfg
        hit = False
        if gun_wrist is not None and off_center is not None:
            d = float(np.linalg.norm(off_center - gun_wrist)) / scale
            before = self.dist.at(t - cfg.slap_window_s)
            self.dist.push(t, d)
            closing = 0.0 if before is None else before - d
            below = off_center[1] > gun_wrist[1] - cfg.slap_above_tol * sw
            self.d = d
            self.last_seen = (t, d, closing, below)
            if d < cfg.slap_watch_m and closing > 0.6 * cfg.slap_closing_m:
                self.suppress_until = max(self.suppress_until, t + cfg.slap_suppress_s)
            hit = d < cfg.slap_near_m and closing > cfg.slap_closing_m and below
        else:
            self.d = None
            self.dist.clear()
            if self.last_seen is not None:
                seen_t, d, closing, below = self.last_seen
                if t - seen_t > cfg.slap_vanish_s:
                    self.last_seen = None
                elif d < cfg.slap_watch_m and closing > cfg.slap_closing_m and below:
                    hit = True          # closing fast from below, then a hand vanished = contact
                    self.last_seen = None
        return hit and self.can_reload(t)
