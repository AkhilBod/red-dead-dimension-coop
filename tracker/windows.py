"""Small time-windowed statistics used to make per-frame estimates robust."""
from collections import deque

import numpy as np


class TimedWindow:
    def __init__(self, seconds):
        self.seconds = seconds
        self.buf = deque()

    def clear(self):
        self.buf.clear()

    def push(self, t, value):
        self.buf.append((t, value))
        while self.buf[0][0] < t - self.seconds:
            self.buf.popleft()

    def values(self):
        return [v for _, v in self.buf]

    def percentile(self, q):
        return float(np.percentile(self.values(), q))

    def median(self):
        return self.percentile(50)

    def at(self, time):
        """Newest value at or before `time`, else None."""
        for t, v in reversed(self.buf):
            if t <= time:
                return v
        return None

    def span(self):
        return self.buf[-1][0] - self.buf[0][0] if self.buf else 0.0
