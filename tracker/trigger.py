"""The two trigger gestures. Each returns the ONSET time of the gesture when it
fires, so the caller can rewind the aim to before the hand started moving.
All lengths are real metres (image distance / the hand's image scale)."""
from windows import TimedWindow


class ThumbTrigger:
    """Hammer drop: the thumb falls by a fraction of its own recent peak.

    Relative on purpose. People differ (some rest thumb-up and drop it, some rest
    thumb-down and pop it up just before each shot), and absolute levels shift with
    the camera angle. A fall of a third from the last 0.4s peak is a shot either way.
    """

    def __init__(self, cfg):
        self.cfg = cfg
        self.window = TimedWindow(cfg.thumb_window_s)
        self.reset()

    def reset(self):
        self.window.clear()
        self.armed = True
        self.trough = float("inf")
        self.below = 0
        self.last_t = None
        self.f = None
        self.peak = None

    def update(self, t, f, steady):
        """steady = the hand is neither moving fast nor rotating. While it is, the thumb
        landmarks are garbage, so those frames are skipped. Skipped, not reset: a real
        trigger pull often comes with a small jerk, and the thumb was up just before it."""
        cfg = self.cfg
        if self.last_t is not None and t - self.last_t > cfg.trigger_gap_reset_s:
            self.reset()
        self.last_t, self.f = t, f
        if f is None or not steady:
            self.below = 0
            return None
        self.window.push(t, f)

        if not self.armed:
            self.trough = min(self.trough, f)
            if f >= self.trough * (1.0 + cfg.thumb_rearm_frac) and f >= cfg.thumb_min_cocked:
                self.armed = True
            self.peak = None
            return None

        self.peak = max(self.window.values())
        fell = self.peak >= cfg.thumb_min_cocked and f <= self.peak * (1.0 - cfg.thumb_drop_frac)
        self.below = self.below + 1 if fell else 0
        if self.below < cfg.thumb_confirm_frames:
            return None
        onset = self.window.buf[0][0]
        for ts, v in reversed(self.window.buf):
            if v >= self.peak * 0.92:      # last moment the thumb was still up
                onset = ts
                break
        self.armed, self.trough, self.below = False, f, 0
        self.window.clear()
        return onset


class FlickTrigger:
    """Recoil kick: the fingertip rises relative to the wrist, i.e. the hand ROTATES up.
    Re-aiming translates the whole hand and barely changes this, so it does not fire."""

    def __init__(self, cfg):
        self.cfg = cfg
        self.window = TimedWindow(cfg.flick_baseline_s)
        self.reset()

    def reset(self):
        self.window.clear()
        self.armed = True
        self.count = 0
        self.last_t = None
        self.rise = 0.0
        self.fired_level = 0.0
        self.fired_rise = 0.0

    def swing(self, seconds):
        """How much the hand has rotated lately. Used to freeze the thumb trigger."""
        if not self.window.buf:
            return 0.0
        now = self.window.buf[-1][0]
        recent = [v for ts, v in self.window.buf if ts >= now - seconds]
        return max(recent) - min(recent)

    def update(self, t, g):
        cfg = self.cfg
        if self.last_t is not None and t - self.last_t > cfg.trigger_gap_reset_s:
            self.reset()
        self.last_t = t
        self.window.push(t, g)
        base = min(v for _, v in self.window.buf)
        self.rise = g - base
        onset = self.window.buf[0][0]
        for ts, v in reversed(self.window.buf):
            if v <= base + 0.2 * self.rise:     # last moment the hand was still level
                onset = ts
                break

        if not self.armed:
            self.count = 0
            came_down = g <= self.fired_level - cfg.flick_rearm_frac * self.fired_rise
            if came_down or self.rise < 0.4 * cfg.flick_rise_m:
                self.armed = True
            return None

        kicked = self.rise >= cfg.flick_rise_m and t - onset <= cfg.flick_max_rise_s
        self.count = self.count + 1 if kicked else 0
        if self.count < cfg.flick_confirm_frames:
            return None
        self.armed, self.count = False, 0
        self.fired_level, self.fired_rise = g, self.rise
        return onset
