"""One Euro filter (Casiez et al. 2012).

A low-pass filter whose cutoff rises with speed: heavy smoothing when the hand
is still (kills jitter), light smoothing when it moves fast (kills lag).
"""
import math

import numpy as np


def _alpha(cutoff, dt):
    tau = 1.0 / (2.0 * math.pi * cutoff)
    return 1.0 / (1.0 + tau / dt)


class OneEuro:
    def __init__(self, min_cutoff, beta, d_cutoff=1.0, max_gap_s=0.5):
        self.min_cutoff = min_cutoff
        self.beta = beta
        self.d_cutoff = d_cutoff
        self.max_gap_s = max_gap_s
        self.reset()

    def reset(self):
        self.x = None
        self.dx = None
        self.t = None

    def __call__(self, x, t):
        x = np.asarray(x, dtype=float)
        if self.x is None or t - self.t > self.max_gap_s:
            self.x, self.dx, self.t = x, np.zeros_like(x), t
            return self.x
        dt = t - self.t
        if dt <= 0:
            return self.x
        a_d = _alpha(self.d_cutoff, dt)
        self.dx = a_d * ((x - self.x) / dt) + (1.0 - a_d) * self.dx
        cutoff = self.min_cutoff + self.beta * float(np.linalg.norm(self.dx))
        a = _alpha(cutoff, dt)
        self.x = a * x + (1.0 - a) * self.x
        self.t = t
        return self.x
