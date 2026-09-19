"""The tuning window: landmarks, crosshair, and every detector's value against its threshold."""
import cv2
import numpy as np

HAND_EDGES = [(0, 1), (1, 2), (2, 3), (3, 4), (0, 5), (5, 6), (6, 7), (7, 8), (5, 9), (9, 10), (10, 11), (11, 12),
              (9, 13), (13, 14), (14, 15), (15, 16), (13, 17), (17, 18), (18, 19), (19, 20), (0, 17)]
FONT = cv2.FONT_HERSHEY_SIMPLEX
GREEN, ORANGE, RED, GRAY, WHITE, YELLOW = (80, 255, 80), (0, 170, 255), (60, 60, 255), (130, 130, 130), (240, 240, 240), (60, 220, 255)
KEYS = "Q quit   X centre crosshair here   [ ] sensitivity   C calibrate   N recenter stance   F recoil trigger on/off   R record   W window"


class DebugView:
    def __init__(self, width=960):
        self.w = width
        self.h = int(width * 9 / 16)
        self.shots = []             # (t, x, y)
        self.flash = ("", 0.0)

    def _px(self, pt_h, aspect):
        """Point in image-height units -> window pixels."""
        return int(pt_h[0] / aspect * self.w), int(pt_h[1] * self.h)

    def _bar(self, img, row, label, value, lo, hi, marks=(), color=GREEN):
        x0, y0, bw, bh = 12, 96 + row * 26, 170, 12
        cv2.putText(img, label, (x0, y0 - 3), FONT, 0.42, WHITE, 1, cv2.LINE_AA)
        x0 += 62
        cv2.rectangle(img, (x0, y0 - bh), (x0 + bw, y0), (50, 50, 50), -1)
        if value is not None:
            k = float(np.clip((value - lo) / (hi - lo), 0, 1))
            zero = float(np.clip((0 - lo) / (hi - lo), 0, 1))
            a, b = sorted((int(x0 + zero * bw), int(x0 + k * bw)))
            cv2.rectangle(img, (a, y0 - bh), (max(b, a + 1), y0), color, -1)
            cv2.putText(img, f"{value:+.2f}", (x0 + bw + 6, y0 - 1), FONT, 0.42, WHITE, 1, cv2.LINE_AA)
        for m, c in marks:
            mx = int(x0 + float(np.clip((m - lo) / (hi - lo), 0, 1)) * bw)
            cv2.line(img, (mx, y0 - bh - 3), (mx, y0 + 3), c, 2)

    def draw(self, bgr, frame, pipeline, state, events, fps, ms, recording=False, low_fps=False, glove=""):
        w, h = self.w, self.h
        img = np.full((h, w, 3), 24, np.uint8) if bgr is None else cv2.resize(bgr, (w, h))
        dbg, body, cfg = pipeline.debug, pipeline.body, pipeline.cfg
        aspect = frame.aspect

        if body.visible:
            ls, rs = self._px(body.pts[11], aspect), self._px(body.pts[12], aspect)
            cv2.line(img, ls, rs, YELLOW, 2, cv2.LINE_AA)
            cv2.circle(img, self._px(body.anchor_raw, aspect), 6, YELLOW, -1, cv2.LINE_AA)
            for chain in ("L", "R"):
                wr = body.wrist(chain, 0.4)
                if wr is not None:
                    cv2.circle(img, self._px(wr, aspect), 5, YELLOW, 1, cv2.LINE_AA)
            hy = int(dbg["holster_y"] * h)
            cv2.line(img, (0, hy), (w, hy), GRAY, 1)
            cv2.putText(img, "holster line", (w - 110, hy - 4), FONT, 0.4, GRAY, 1, cv2.LINE_AA)
            nx = int(body.neutral_x / aspect * w)
            cv2.line(img, (nx, 0), (nx, 18), GRAY, 2)
            sy = int(body.stand_y * h)
            cv2.line(img, (w - 18, sy), (w, sy), GRAY, 2)

        for hand in pipeline.ignored:
            pts = [(int(p[0] * w), int(p[1] * h)) for p in hand.pts]
            for a, b in HAND_EDGES:
                cv2.line(img, pts[a], pts[b], (60, 60, 160), 1, cv2.LINE_AA)
            cv2.putText(img, "ignored", (pts[0][0] + 8, pts[0][1] + 16), FONT, 0.45, (60, 60, 200), 1, cv2.LINE_AA)

        for info in dbg.get("infos", []):
            color = GREEN if info is dbg.get("gun") else ORANGE
            pts = [(int(p[0] * w), int(p[1] * h)) for p in info.hand.pts]
            for a, b in HAND_EDGES:
                cv2.line(img, pts[a], pts[b], color, 1, cv2.LINE_AA)
            tag = "GUN" if info is dbg.get("gun") else ("open" if info.open else "hand")
            cv2.putText(img, tag, (pts[0][0] + 8, pts[0][1] + 16), FONT, 0.5, color, 1, cv2.LINE_AA)

        target = pipeline.mapper.pending
        if target is not None:
            tx, ty = int(target[0] * w), int(target[1] * h)
            cv2.circle(img, (tx, ty), 26, RED, 3, cv2.LINE_AA)
            cv2.circle(img, (tx, ty), 5, RED, -1, cv2.LINE_AA)
            cv2.putText(img, f"POINT AT THE RED TARGET AND FIRE ({len(pipeline.mapper.pairs) + 1}/{cfg.calib_points})",
                        (w // 2 - 250, h - 40), FONT, 0.7, RED, 2, cv2.LINE_AA)

        for e in events:
            if e[0] == "fire":
                self.shots.append((frame.t, e[1], e[2]))
                self.flash = ("FIRE", frame.t + 0.15)
            elif e[0] == "reload":
                self.flash = ("RELOAD", frame.t + 0.35)
        self.shots = [s for s in self.shots if frame.t - s[0] < 1.5]
        for _, x, y in self.shots:
            cv2.drawMarker(img, (int(x * w), int(y * h)), RED, cv2.MARKER_TILTED_CROSS, 18, 2, cv2.LINE_AA)

        cx, cy = int(state.aim_x * w), int(state.aim_y * h)
        color = GREEN if state.aim_valid else GRAY
        cv2.circle(img, (cx, cy), 16, color, 2, cv2.LINE_AA)
        cv2.line(img, (cx - 26, cy), (cx + 26, cy), color, 1, cv2.LINE_AA)
        cv2.line(img, (cx, cy - 26), (cx, cy + 26), color, 1, cv2.LINE_AA)

        for x0, y0, x1, y1 in ((0, 0, 860, 60), (0, 60, 320, 240), (0, h - 30, w, h)):
            img[y0:y1, x0:x1] = (img[y0:y1, x0:x1] * 0.35).astype(np.uint8)     # dark backing so text reads over video
        cv2.putText(img, f"{fps:4.1f} fps  {ms:4.1f} ms   gun arm {dbg.get('gun_arm') or '?'}   "
                         f"gain {cfg.aim_gain:.2f} x lever {dbg.get('lever', 0):.1f}   {'calibrated' if pipeline.mapper.calibrated else ('centred' if pipeline.mapper.centered else 'centring...')}"
                         f"   {glove}{'   REC' if recording else ''}", (12, 22), FONT, 0.55, WHITE, 1, cv2.LINE_AA)
        flags = [("TRACK", state.tracking), ("GUN", state.gun_pose), ("AIM", state.aim_valid),
                 ("HOLSTER", state.holstered), ("OPEN", state.off_hand_open)]
        x = 12
        for name, on in flags:
            cv2.putText(img, name, (x, 48), FONT, 0.55, GREEN if on else (90, 90, 90), 2 if on else 1, cv2.LINE_AA)
            x += 22 + 11 * len(name)

        thumb, flick, reload_ = pipeline.thumb, pipeline.flick, pipeline.reload
        has_gun = dbg.get("gun") is not None
        steady = dbg.get("steady", False)
        marks = [(cfg.thumb_min_cocked, GREEN)]
        if thumb.peak:
            marks.append((thumb.peak * (1.0 - cfg.thumb_drop_frac), RED))
        self._bar(img, 0, "thumb", thumb.f if (has_gun and thumb.f is not None) else None, 0.0, 1.2, marks=marks,
                  color=GREEN if (thumb.armed and steady) else GRAY)       # gray = frozen (hand moving) or waiting to re-arm
        self._bar(img, 1, "kick m", flick.rise if has_gun else None, -0.02, 0.15,
                  marks=((cfg.flick_rise_m, RED),), color=GREEN if flick.armed else GRAY)
        self._bar(img, 2, "hand v", pipeline.hand_speed if has_gun else None, 0.0, 1.0, marks=((cfg.thumb_max_speed, RED),))
        rel = reload_.relation
        self._bar(img, 3, "hands dx", None if rel is None else float(rel[0]), -2.0, 2.0,
                  marks=((-cfg.slap_together_dx, YELLOW), (cfg.slap_together_dx, YELLOW)),
                  color=YELLOW if reload_.together(frame.t) else GREEN)     # yellow = hands together: a kick now is a reload
        self._bar(img, 4, "lean", state.lean, -1.0, 1.0)
        self._bar(img, 5, "duck", state.duck, 0.0, 1.0)

        if low_fps:
            cv2.rectangle(img, (0, h // 2 - 34), (w, h // 2 + 22), (0, 0, 120), -1)
            cv2.putText(img, f"LOW FRAME RATE ({fps:.0f} fps): nothing here can be trusted. Close other apps.", (20, h // 2 + 4), FONT, 0.75, WHITE, 2, cv2.LINE_AA)
        if frame.t < self.flash[1]:
            cv2.putText(img, self.flash[0], (w // 2 - 80, 70), cv2.FONT_HERSHEY_DUPLEX, 1.5, YELLOW, 3, cv2.LINE_AA)
        cv2.putText(img, KEYS, (12, h - 12), FONT, 0.4, WHITE, 1, cv2.LINE_AA)
        return img
