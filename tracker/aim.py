"""Raw aim (fingertip position relative to the chest, real metres) -> screen coordinates."""
from collections import deque

import numpy as np


class AimHistory:
    """Recent raw aim, so a shot can use where the hand was BEFORE the trigger motion."""

    def __init__(self, seconds=1.0):
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

    def settled_before(self, time, window):
        """Median aim over the `window` seconds ending at `time`.

        A trigger gesture is only recognisable once it is under way, so its detected
        onset is already a few frames into the motion and a single sample there is
        already off target. The player held their aim BEFORE that, so use that stretch.
        """
        pts = [raw for t, raw in self.buf if time - window <= t <= time]
        if len(pts) < 2:
            return self.at(time)
        return np.median(np.array(pts), axis=0)


class AimMapper:
    """screen = 0.5 + k * lever * (raw - centre) / screen size, per axis.

    raw   = fingertip position relative to the chest, real metres.
    lever = how much further a ray from the eyes through the fingertip moves on the screen
            than the fingertip itself does: eye distance / arm reach, about 2 at a laptop.
            This is what makes the crosshair travel as far as the finger points.
    k     = calibration's correction to that geometric gain, 1 until calibrated.

    The centre cannot be computed: people do not sight down their finger, they hold the
    hand off to one side and point "from the hip", and nothing in the landmarks says where
    that arm thinks the middle of the screen is. So it is learned:
      - the first place a player steadily points counts as the middle of the screen,
      - pushing past a screen edge drags the centre along, exactly like a mouse at the edge
        of a monitor, so the crosshair can never sit stuck outside the screen,
      - calibration shots from the game replace both k and the centre.
    """

    def __init__(self, cfg):
        self.cfg = cfg
        self.size = np.array([cfg.screen_width_m, cfg.screen_width_m / cfg.screen_aspect])
        self.k = np.ones(2)
        self.center = None
        self.calibrated = False
        self.active = False
        self.pending = None
        self.pairs = []

    @property
    def centered(self):
        return self.center is not None

    def center_on(self, raw):
        self.center = np.array(raw, dtype=float)

    def forget_center(self):
        """Calibrated or not. Calibration's lasting value is the gain (k). The centre belongs
        to a posture: seen live, a player calibrated, walked off, sat back down differently,
        and the crosshair stayed pinned to the left edge for the rest of the session."""
        self.center = None

    def map(self, raw, lever, push=True):
        if self.center is None or raw is None:
            return 0.5, 0.5
        slope = self.k * lever / self.size
        s = 0.5 + slope * (raw - self.center)
        clipped = np.clip(s, 0.0, 1.0)
        if push:
            self.center += (s - clipped) / slope
        return float(clipped[0]), float(clipped[1])

    def begin(self):
        self.active = True
        self.pending = None
        self.pairs = []
        self.calibrated = False
        self.k = np.ones(2)

    def set_target(self, sx, sy):
        if not self.active:
            self.begin()
        self.pending = np.array([sx, sy], dtype=float)

    def add_shot(self, raw, lever):
        """Pair a shot with the pending target. True if the shot was used."""
        if not self.active or self.pending is None or raw is None:
            return False
        self.pairs.append((np.array(raw, dtype=float), self.pending, float(lever)))
        self.pending = None
        self._solve()
        if len(self.pairs) >= self.cfg.calib_points:
            self.active = False
        return True

    def _solve(self):
        """Least squares per axis. k may only move so far from 1: four noisy shots should
        correct the geometric gain, not replace it with something twitchy."""
        cfg = self.cfg
        raws = np.array([p[0] for p in self.pairs])
        targets = np.array([p[1] for p in self.pairs])
        lever = float(np.median([p[2] for p in self.pairs]))
        k = np.ones(2)
        for axis in range(2):
            r, s = raws[:, axis], targets[:, axis]
            if np.ptp(s) > 0.2 and np.ptp(r) > 1e-6:
                slope = float(np.polyfit(r, s, 1)[0])
                if slope > 0:
                    k[axis] = float(np.clip(slope * self.size[axis] / lever, cfg.calib_gain_min, cfg.calib_gain_max))
        self.k = k
        self.center = raws.mean(axis=0) - (targets.mean(axis=0) - 0.5) * self.size / (k * lever)
        self.calibrated = True
