"""The two trigger gestures. Each returns the ONSET time of the gesture when it
fires, so the caller can rewind the aim to before the hand started moving."""
from collections import deque


def value_at(hist, time):
    """Newest sample at or before `time` in a deque of (t, value). None if too early."""
    for t, v in reversed(hist):
        if t <= time:
            return v
    return None


class ThumbTrigger:
    def __init__(self, cfg):
        self.cfg = cfg
        self.reset()

    def reset(self):
        self.hist = deque()
        self.hi = None
        self.armed = False
        self.below = 0
        self.last_t = None
        self.f = None

    def levels(self):
        hi = self.hi or self.cfg.thumb_min_cocked
        return hi * self.cfg.thumb_fire_frac, hi * self.cfg.thumb_rearm_frac

    def update(self, t, f):
        cfg = self.cfg
        if self.last_t is not None and t - self.last_t > cfg.trigger_gap_reset_s:
            self.reset()
        dt = 0.0 if self.last_t is None else t - self.last_t
        self.last_t, self.f = t, f
        self.hist.append((t, f))
        while self.hist[0][0] < t - cfg.thumb_window_s:
            self.hist.popleft()

        if self.hi is None:
            self.hi = f
        else:
            tau = cfg.thumb_hi_rise_s if f > self.hi else cfg.thumb_hi_fall_s
            self.hi += (f - self.hi) * min(1.0, dt / tau)
        self.hi = max(self.hi, cfg.thumb_min_cocked)
        fire_level, rearm_level = self.levels()

        if not self.armed:
            self.below = 0
            if f > rearm_level:
                self.armed = True
            return None

        self.below = self.below + 1 if f < fire_level else 0
        if self.below < cfg.thumb_confirm_frames:
            return None
        peak = max(v for _, v in self.hist)
        if peak - f < cfg.thumb_drop_frac * self.hi:
            return None   # drifted down slowly, not a trigger pull
        self.armed = False
        self.below = 0
        # Onset = last moment the thumb was still near the top.
        for ts, v in reversed(self.hist):
            if v >= peak * 0.92:
                return ts
        return self.hist[0][0]


class FlickTrigger:
    def __init__(self, cfg):
        self.cfg = cfg
        self.reset()

    def reset(self):
        self.hist = deque()
        self.armed = True
        self.count = 0
        self.last_t = None
        self.rise = 0.0

    def update(self, t, g):
        cfg = self.cfg
        if self.last_t is not None and t - self.last_t > cfg.trigger_gap_reset_s:
            self.reset()
        self.last_t = t
        self.hist.append((t, g))
        while self.hist[0][0] < t - 0.6:
            self.hist.popleft()

        before = value_at(self.hist, t - cfg.flick_window_s)
        earlier = value_at(self.hist, t - cfg.flick_window_s - cfg.flick_quiet_s)
        if before is None or earlier is None:
            self.rise = 0.0
            return None
        self.rise = g - before
        if not self.armed:
            self.count = 0
            if self.rise < cfg.flick_rearm:
                self.armed = True
            return None
        quiet = abs(before - earlier) < cfg.flick_quiet
        self.count = self.count + 1 if (self.rise > cfg.flick_rise and quiet) else 0
        if self.count < cfg.flick_confirm_frames:
            return None
        self.armed = False
        self.count = 0
        return t - cfg.flick_window_s
