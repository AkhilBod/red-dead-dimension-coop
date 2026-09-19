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
    """screen = 0.5 + k * (raw - centre) / span, per axis.

    raw  = how far the fingertip has moved, relative to the chest, in real metres.
    span = metres of fingertip travel that cross the screen (cfg.aim_span_m wide, and the
           matching height). A fixed, physical sensitivity: it feels the same seated at a
           laptop or standing across a room, and it does not change while you aim.
    k    = calibration's correction to that, 1 until calibrated.

    This is deliberately a steady, slightly heavy pointer and not a "true" one. Working out
    where a finger really points was tried on recorded sessions and cannot be done from these
    landmarks (see config.py), and a twitchy crosshair feels nothing like a gun.

    The centre cannot be computed either: people hold the hand off to one side and point
    "from the hip", and nothing says where that arm thinks the middle of the screen is. So:
      - where a player first points counts as the middle of the screen,
      - holding the hand past a screen edge slowly pulls the centre along, so the crosshair
        never stays lost. Slowly on purpose: flicking into a corner and back must NOT move the
        mapping (seen live: an instant version left the crosshair off after every corner),
      - calibration shots from the game replace both k and the centre.
    """

    def __init__(self, cfg):
        self.cfg = cfg
        self.k = np.ones(2)
        self.center = None
        self.calibrated = False
        self.active = False
        self.pending = None
        self.pairs = []

    @property
    def span(self):
        return np.array([self.cfg.aim_span_m, self.cfg.aim_span_m / self.cfg.screen_aspect])

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

    def map(self, raw, dt=0.0):
        """dt > 0 lets a hand held past an edge pull the centre along. dt = 0 only reads."""
        if self.center is None or raw is None:
            return 0.5, 0.5
        slope = self.k / self.span
        s = 0.5 + slope * (raw - self.center)
        clipped = np.clip(s, 0.0, 1.0)
        if dt > 0:
            # A steady pace, not a share of the overshoot: a quick flick far past the corner must
            # move the mapping no more than a small one does.
            step = self.cfg.aim_edge_pull_rate * dt
            self.center += np.clip(s - clipped, -step, step) / slope
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
        """Least squares per axis. k may only move so far from 1: four noisy shots should
        correct the sensitivity, not replace it with something twitchy."""
        cfg = self.cfg
        raws = np.array([p[0] for p in self.pairs])
        targets = np.array([p[1] for p in self.pairs])
        k = np.ones(2)
        for axis in range(2):
            r, s = raws[:, axis], targets[:, axis]
            if np.ptp(s) > 0.2 and np.ptp(r) > 1e-6:
                slope = float(np.polyfit(r, s, 1)[0])
                if slope > 0:
                    k[axis] = float(np.clip(slope * self.span[axis], cfg.calib_gain_min, cfg.calib_gain_max))
        self.k = k
        self.center = raws.mean(axis=0) - (targets.mean(axis=0) - 0.5) * self.span / k
        self.calibrated = True
