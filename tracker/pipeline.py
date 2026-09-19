"""Landmarks in, game state and events out. No camera, no OSC, no drawing, so it
can be tested and replayed offline."""
from dataclasses import dataclass

import numpy as np

import protocol as P
from aim import AimHistory, AimMapper
from body import BodyTracker
from hand_features import INDEX_MCP, INDEX_TIP, PALM, WRIST, extensions, is_gun_pose, is_open_palm, iso2d, thumb_feature
from one_euro import OneEuro
from reload import ReloadDetector
from trigger import FlickTrigger, ThumbTrigger


@dataclass
class HandInfo:
    hand: object
    p2: np.ndarray          # (21, 2) in H units
    wrist: np.ndarray
    center: np.ndarray
    gun: bool
    open: bool
    raised: bool


def _dist(a, b):
    return float(np.linalg.norm(a - b))


class Pipeline:
    def __init__(self, cfg):
        self.cfg = cfg
        self.body = BodyTracker(cfg)
        self.mapper = AimMapper(cfg)
        self.history = AimHistory()
        self.aim_filter = OneEuro(cfg.aim_min_cutoff, cfg.aim_beta)
        self.thumb = ThumbTrigger(cfg)
        self.flick = FlickTrigger(cfg)
        self.reload = ReloadDetector(cfg)
        self.aim = (0.5, 0.5)
        self.gun_pos = None
        self.gun_seen_t = -1e9
        self.chain = None           # which arm of the body model holds the gun, "L" or "R"
        self.chain_votes = 0
        self.side = 1               # +1 if that arm is on the right of the mirrored image
        self.last_fire_t = -1e9
        self.held_shot = None       # (release time, raw aim) while we wait to see if it was a slap
        self.gun_pose = False
        self.gun_pose_frames = 0
        self.gun_pose_true_t = -1e9
        self.off_open = False
        self.off_open_since = None
        self.off_open_true_t = -1e9
        self.down_since = None
        self.debug = {}

    def handle_command(self, address, args):
        if address == P.ADDR_CALIB_BEGIN:
            self.mapper.begin()
            self.body.recenter()
        elif address == P.ADDR_CALIB_TARGET and len(args) >= 2:
            self.mapper.set_target(args[0], args[1])
        elif address == P.ADDR_RECENTER:
            self.body.recenter()

    def _emit_fire(self, events, raw):
        x, y = self.mapper.map(raw, self.side)
        self.mapper.add_shot(raw)
        events.append(("fire", x, y))

    def _drop_gun_lock(self):
        self.gun_pos = None
        self.aim_filter.reset()
        self.history.clear()
        self.thumb.reset()
        self.flick.reset()

    def _pick_gun(self, infos, t, sw):
        cfg = self.cfg
        raised = [h for h in infos if h.raised]
        if self.gun_pos is not None:
            if t - self.gun_seen_t > cfg.gun_unlock_s:
                self._drop_gun_lock()
            else:
                near = [h for h in raised if _dist(h.wrist, self.gun_pos) < cfg.gun_lock_radius * sw]
                if not near:
                    return None
                return min(near, key=lambda h: (not h.gun, _dist(h.wrist, self.gun_pos)))
        candidates = [h for h in raised if not h.open]
        if not candidates:
            return None
        return min(candidates, key=lambda h: (not h.gun, h.wrist[1]))

    def _update_side(self, gun, anchor, sw, fresh):
        body = self.body
        best = None
        for chain in ("L", "R"):
            w = body.wrist(chain, 0.4)
            if w is not None:
                d = _dist(w, gun.wrist)
                if d < 0.6 * sw and (best is None or d < best[1]):
                    best = (chain, d)
        if best is not None:
            if best[0] == self.chain:
                self.chain_votes = 0
            else:
                self.chain_votes += 1
                if self.chain is None or self.chain_votes >= 8:
                    self.chain, self.chain_votes = best[0], 0
        if self.chain is not None and body.visible:
            self.side = 1 if body.shoulder(self.chain)[0] >= anchor[0] else -1
        elif self.chain is None and fresh:
            # No arm matched yet. Guess once per lock: re-guessing every frame would
            # flip the crosshair whenever the hand crossed the midline.
            self.side = 1 if gun.wrist[0] >= anchor[0] else -1

    def update(self, frame):
        cfg, body = self.cfg, self.body
        t, aspect = frame.t, frame.aspect
        events = []

        body.update(t, frame.pose, aspect)
        if body.anchor is not None and body.tracking(t):
            anchor, aim_anchor, sw = body.anchor, body.anchor_raw, body.sw
            holster_y = body.holster_y()
        else:
            anchor = aim_anchor = np.array([0.5 * aspect, 0.55])
            sw, holster_y = cfg.fallback_sw, 0.85

        infos = []
        for hand in frame.hands:
            p2 = iso2d(hand.pts, aspect)
            ext = extensions(hand, aspect)
            infos.append(HandInfo(hand, p2, p2[WRIST], p2[list(PALM)].mean(axis=0),
                                  is_gun_pose(ext, cfg), is_open_palm(ext, cfg), p2[WRIST][1] < holster_y))

        had_lock = self.gun_pos is not None
        gun = self._pick_gun(infos, t, sw)
        off = next((h for h in infos if h is not gun), None) if gun is not None else None

        onset = None
        if gun is not None:
            fresh = not had_lock or self.gun_pos is None
            if fresh:
                self._drop_gun_lock()      # start filters and triggers clean
            self._update_side(gun, anchor, sw, fresh)
            point = (1.0 - cfg.aim_tip_weight) * gun.p2[INDEX_MCP] + cfg.aim_tip_weight * gun.p2[INDEX_TIP]
            raw = self.aim_filter((point - aim_anchor) / sw, t)
            self.history.push(t, raw)
            self.aim = self.mapper.map(raw, self.side)
            self.gun_pos, self.gun_seen_t = gun.wrist, t

            f = thumb_feature(gun.hand, aspect)
            if f is not None:
                onset = self.thumb.update(t, f)
            flick_onset = self.flick.update(t, (gun.p2[WRIST][1] - gun.p2[INDEX_TIP][1]) / sw)
            if cfg.flick_enabled and flick_onset is not None:
                onset = flick_onset if onset is None else min(onset, flick_onset)

        # Reload runs before the fire decision so a slap can veto the shot it causes.
        pose_gun = pose_off = None
        if self.chain is not None and body.visible:
            pose_gun = body.wrist(self.chain)
            pose_off = body.wrist("R" if self.chain == "L" else "L")
        recently_armed = t - self.gun_seen_t < 1.0
        if self.reload.update(t, gun.wrist if gun else None, off.center if off else None, pose_gun, pose_off, sw) and recently_armed:
            events.append(("reload",))
            self.held_shot = None           # that "shot" was the slap knocking the hand

        if onset is not None and t - self.last_fire_t >= cfg.fire_cooldown_s and not self.reload.fire_suppressed(t):
            raw_then = self.history.at(max(onset - cfg.rewind_margin_s, t - cfg.rewind_max_s))
            self.last_fire_t = t
            # The slap's knock can look like a trigger pull a few frames BEFORE the reload is
            # recognised. If the other hand is close, wait a moment before committing to the
            # shot. With the other hand out of the way (the normal case) nothing is delayed.
            near = [p for p in (off.center if off else None, pose_off) if p is not None]
            if gun is not None and any(_dist(p, gun.wrist) < cfg.fire_hold_radius * sw for p in near):
                self.held_shot = (t + cfg.fire_hold_s, raw_then)
            else:
                self._emit_fire(events, raw_then)
        if self.held_shot is not None and t >= self.held_shot[0]:
            self._emit_fire(events, self.held_shot[1])
            self.held_shot = None

        # Debounced flags.
        if gun is not None and gun.gun:
            self.gun_pose_frames += 1
            self.gun_pose_true_t = t
        else:
            self.gun_pose_frames = 0
        if self.gun_pose_frames >= cfg.pose_on_frames:
            self.gun_pose = True
        elif t - self.gun_pose_true_t > cfg.pose_off_s:
            self.gun_pose = False

        open_line = anchor[1] + cfg.offhand_raise_line * sw
        if any(h.open and h.wrist[1] < open_line for h in infos if h is not gun):
            self.off_open_true_t = t
            if self.off_open_since is None:
                self.off_open_since = t
            if t - self.off_open_since >= cfg.offhand_on_s:
                self.off_open = True
        else:
            self.off_open_since = None
            if t - self.off_open_true_t > cfg.offhand_off_s:
                self.off_open = False

        # Holstered = nothing raised. The body model's wrist backs up the hand
        # model, so a hand-tracking dropout while aiming does not read as a holster.
        up = any(h.raised for h in infos)
        if not up and body.visible:
            for chain in ((self.chain,) if self.chain else ("L", "R")):
                w = body.wrist(chain, 0.6)
                if w is not None and w[1] < holster_y - 0.05 * sw:
                    up = True
        if up:
            self.down_since = None
        elif self.down_since is None:
            self.down_since = t
        tracking = body.tracking(t)
        holstered = tracking and not up and t - self.down_since >= cfg.holster_delay_s

        self.debug = {
            "infos": infos, "gun": gun, "off": off, "anchor": anchor, "sw": sw, "holster_y": holster_y,
            "body_visible": body.visible, "side": self.side, "chain": self.chain,
        }
        state = P.State(
            aim_x=self.aim[0], aim_y=self.aim[1],
            aim_valid=float(t - self.gun_seen_t <= cfg.aim_hold_s),
            gun_pose=float(self.gun_pose), holstered=float(holstered),
            lean=body.lean, duck=body.duck, body_speed=body.speed,
            tracking=float(tracking), off_hand_open=float(self.off_open),
        )
        return state, events
