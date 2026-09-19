"""Raw aim (hand position relative to the chest, in real metres) -> screen coordinates."""
from collections import deque

import numpy as np


class AimHistory:
    """Recent raw aim, so a shot can use where the hand was BEFORE the trigger motion."""

    def __init__(self, seconds=0.8):
        self.seconds = seconds
        self.buf = deque()

    def clear(self):
        self.buf.clear()

    def push(self, t, raw):
        self.buf.append((t, np.array(raw, dtype=float)))
        while self.buf[0][0] < t - self.seconds:
            self.buf.popleft()

    def at(self, time):
        if not self.buf:
            return None
        for t, raw in reversed(self.buf):
            if t <= time:
                return raw
        return self.buf[0][1]


class AimMapper:
    def __init__(self, cfg):
        self.cfg = cfg
        self.span = np.array([cfg.aim_span_x, cfg.aim_span_x / cfg.screen_aspect])
        self.gain = None            # set by calibration: screen = gain * raw + offset
        self.offset = None
        self.active = False
        self.pending = None
        self.pairs = []

    @property
    def calibrated(self):
        return self.gain is not None

    def map(self, raw, side):
        if self.calibrated:
            s = self.gain * raw + self.offset
        else:
            center = np.array([self.cfg.aim_center_x * side, self.cfg.aim_center_y])
            s = 0.5 + (raw - center) / self.span
        return float(np.clip(s[0], 0.0, 1.0)), float(np.clip(s[1], 0.0, 1.0))

    def begin(self):
        self.active = True
        self.pending = None
        self.pairs = []
        self.gain = None
        self.offset = None

    def set_target(self, sx, sy):
        if not self.active:
            self.begin()
        self.pending = np.array([sx, sy], dtype=float)

    def add_shot(self, raw):
        """Pair a shot with the pending target. True if the shot was used."""
        if not self.active or self.pending is None or raw is None:
            return False
        self.pairs.append((np.array(raw, dtype=float), self.pending))
        self.pending = None
        self._solve()
        if len(self.pairs) >= self.cfg.calib_points:
            self.active = False
        return True

    def _solve(self):
        """Least squares per axis: screen = gain * raw + offset.

        The gain is clamped (aim_span_min/max are metres of hand travel per screen
        width). Pointing naturally at a small screen moves the hand only a few
        centimetres, and a gain that high turns landmark jitter into a shaking
        crosshair. The offset (where this player's hand rests, which hand they use)
        is the part of calibration that matters most.
        """
        cfg = self.cfg
        raws = np.array([p[0] for p in self.pairs])
        targets = np.array([p[1] for p in self.pairs])
        default_gain = 1.0 / self.span
        limits = (
            (1.0 / cfg.aim_span_max, 1.0 / cfg.aim_span_min),
            (cfg.screen_aspect / cfg.aim_span_max, cfg.screen_aspect / cfg.aim_span_min),
        )
        gain = np.zeros(2)
        for axis in range(2):
            r, s = raws[:, axis], targets[:, axis]
            g = default_gain[axis]
            if np.ptp(s) > 0.2 and np.ptp(r) > 1e-6:
                g = float(np.polyfit(r, s, 1)[0])
                if g <= 0:
                    g = default_gain[axis]
            gain[axis] = float(np.clip(g, *limits[axis]))
        self.gain = gain
        self.offset = targets.mean(axis=0) - gain * raws.mean(axis=0)
