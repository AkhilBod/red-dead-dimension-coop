"""Landmarks in, game state and events out. No camera, no OSC, no drawing, so it
can be tested and replayed offline."""
from dataclasses import dataclass

import numpy as np

import protocol as P
from aim import AimHistory, AimMapper
from body import BodyTracker
from hand_features import HAND_LENGTH_M, INDEX_TIP, PALM, WRIST, image_scale, is_open_palm, iso2d, thumb_feature
from one_euro import OneEuro
from reload import ReloadDetector
from trigger import FlickTrigger, ThumbTrigger
from windows import TimedWindow


@dataclass
class HandInfo:
    hand: object
    p2: np.ndarray          # (21, 2) in H units
    wrist: np.ndarray
    center: np.ndarray
    scale: float            # H per metre at this hand's depth, this frame
    length: float           # the hand's length in the image, H
    arm: str = None         # which arm of the body model it hangs off, "L" / "R" / None
    open: bool = False
    raised: bool = False


def _dist(a, b):
    return float(np.linalg.norm(a - b))


def _other(arm):
    return "R" if arm == "L" else "L"


class Pipeline:
    def __init__(self, cfg):
        self.cfg = cfg
        self.body = BodyTracker(cfg)
        self.mapper = AimMapper(cfg)
        self.history = AimHistory()
        self.aim_filter = OneEuro(cfg.aim_min_cutoff, cfg.aim_beta)
        self.gun_scale = TimedWindow(cfg.hand_scale_window_s)
        self.thumb = ThumbTrigger(cfg)
        self.flick = FlickTrigger(cfg)
        # With the hands together a kick can only be a slap, so it may be a gentler one than a shot needs.
        self.slap_kick = FlickTrigger(cfg, cfg.slap_kick_m)
        self.off_flick = FlickTrigger(cfg, cfg.slap_kick_m)     # the other hand's kicks only ever mean "reload"
        self.reload = ReloadDetector(cfg)
        self.aim = (0.5, 0.5)
        self.gun_pos = None
        self.gun_center = None
        self.gun_seen_t = -1e9
        self.hand_speed = 0.0       # m/s
        self.ref_tip = None         # image points the aim measures travel from (fingertip, chest)
        self.ref_chest = None
        self.still_since = None     # for learning the aim centre
        self.centering = []
        self.lock_t = None
        self.recenter_aim = False
        self.gun_arm = None         # which arm holds the gun. Sticky: it is who the player is, not where a hand is
        self.arm_votes = 0
        self.other_arm_since = None
        self.last_fire_t = -1e9
        self.last_fire_kind = None
        self.pending_kick = None    # (decide-by time, raw aim): a kick that may yet turn out to be a slap
        self.button_presses = []    # press times from the glove's trigger switch, handled on the next frame
        self.gun_pose = False
        self.gun_pose_frames = 0
        self.gun_pose_true_t = -1e9
        self.off_open = False
        self.off_open_since = None
        self.off_open_true_t = -1e9
        self.down_since = None
        self.ignored = []           # detections judged not to be the player's hands, for the debug view
        self.debug = {}

    def handle_command(self, address, args):
        if address == P.ADDR_CALIB_BEGIN:
            self.mapper.begin()
            self.body.recenter()
        elif address == P.ADDR_CALIB_TARGET and len(args) >= 2:
            self.mapper.set_target(args[0], args[1])
        elif address == P.ADDR_RECENTER:
            self.body.recenter()
            self.recenter_aim = True

    def press_button(self, t_press):
        """The glove's trigger switch was pressed at time.monotonic() = t_press."""
        self.button_presses.append(t_press)

    # ---- which detections are the player's hands --------------------------------

    def _arm_of(self, h):
        best = None
        for arm in ("L", "R"):
            w = self.body.wrist(arm, 0.3)
            if w is not None:
                d = _dist(w, h.wrist)
                if d <= self.cfg.hand_arm_match * h.length and (best is None or d < best[1]):
                    best = (arm, d)
        return best[0] if best else None

    def _hands(self, frame, chest_scale, holster_y):
        """[rec] A background object came back as a 0.99-confidence hand a third the size of the
        real one and stole the aim whenever the real hand dropped out. A booth full of spectators
        will do the same. A hand only counts if it could physically be the player's."""
        cfg, body = self.cfg, self.body
        infos, self.ignored = [], []
        for hand in sorted(frame.hands, key=lambda h: -h.score):
            p2 = iso2d(hand.pts, frame.aspect)
            scale = image_scale(p2)
            h = HandInfo(hand, p2, p2[WRIST], p2[list(PALM)].mean(axis=0), scale, HAND_LENGTH_M * scale)
            if body.visible:
                h.arm = self._arm_of(h)
                # Farther from the camera than the player's own chest, or not on either arm
                # and not clearly held out in front: not the player's hand.
                if scale < cfg.hand_min_scale_ratio * chest_scale or (h.arm is None and scale < cfg.hand_free_scale_ratio * chest_scale):
                    self.ignored.append(hand)
                    continue
            # The hand model sometimes reports one hand twice. Keep the more confident one.
            if any(_dist(h.center, k.center) < cfg.hand_dedupe * max(h.length, k.length) for k in infos):
                self.ignored.append(hand)
                continue
            h.open = is_open_palm(hand, p2, scale, cfg)
            h.raised = bool(h.wrist[1] < holster_y)
            infos.append(h)
        return infos

    def _drop_gun_lock(self):
        self.gun_pos = None
        self.gun_center = None
        self.hand_speed = 0.0
        self.still_since = None
        self.centering = []
        self.lock_t = None
        self.aim_filter.reset()
        self.history.clear()
        self.gun_scale.clear()
        self.thumb.reset()
        self.flick.reset()
        self.slap_kick.reset()
        self.pending_kick = None

    def _pick_gun(self, infos, t, hands_apart):
        """[rec] During reload slaps the hands overlap, and a lock held by position alone slid
        onto the slapping hand and mirrored the aim. The model's Left/Right labels flip with
        hand orientation, so identity comes from the body model instead: the gun is the hand
        on the gun ARM."""
        cfg = self.cfg
        raised = [h for h in infos if h.raised]
        if self.gun_arm is not None and hands_apart:
            mine = [h for h in raised if h.arm != _other(self.gun_arm)]
        else:
            mine = raised           # arms cannot be told apart while the wrists touch
        if self.gun_pos is not None:
            if t - self.gun_seen_t > cfg.gun_unlock_s:
                self._drop_gun_lock()
            else:
                near = [h for h in mine if _dist(h.wrist, self.gun_pos) < cfg.gun_lock_jump * h.length]
                return min(near, key=lambda h: _dist(h.wrist, self.gun_pos)) if near else None
        candidates = [h for h in mine if not h.open]
        if candidates:
            self.other_arm_since = None
            # The hand nearest the camera is the one held out toward the screen.
            return max(candidates, key=lambda h: h.scale)
        # Only the other arm's hand is up. If it stays that way, the player switched hands.
        others = [h for h in raised if not h.open] if hands_apart else []
        if not others:
            self.other_arm_since = None
            return None
        if self.other_arm_since is None:
            self.other_arm_since = t
        if t - self.other_arm_since >= cfg.hand_switch_s:
            self.gun_arm, self.arm_votes, self.other_arm_since = others[0].arm, 0, None
            return max(others, key=lambda h: h.scale)
        return None

    def _learn_gun_arm(self, gun):
        if self.gun_arm is None and gun.arm is not None:
            self.arm_votes += 1
            if self.arm_votes >= 5:
                self.gun_arm = gun.arm

    # ---- events -------------------------------------------------------------------

    def _fire(self, events, t, raw, kind):
        cfg = self.cfg
        since = t - self.last_fire_t
        if since < cfg.fire_cooldown_s or self.reload.fire_suppressed(t):
            return
        if kind != self.last_fire_kind and since < cfg.other_trigger_lockout_s:
            return                  # thumb drop followed by its own recoil kick: one shot
        x, y = self.mapper.map(raw)
        self.mapper.add_shot(raw)
        events.append(("fire", x, y, kind))
        self.last_fire_t, self.last_fire_kind = t, kind

    def _reload(self, events, t):
        self.pending_kick = None
        if self.reload.can_reload(t):
            self.reload.mark_reload(t)
            events.append(("reload",))

    def _rewound(self, t, onset):
        cfg = self.cfg
        return self.history.settled_before(max(onset - cfg.rewind_margin_s, t - cfg.rewind_max_s), cfg.rewind_window_s)

    def update(self, frame):
        cfg, body = self.cfg, self.body
        t, aspect = frame.t, frame.aspect
        events = []

        body.update(t, frame.pose, aspect)
        if body.anchor is not None:
            # Also while the body is briefly lost: a stale chest barely matters, a made-up one would jump the aim.
            anchor, aim_anchor, sw = body.anchor, body.anchor_raw, body.sw
            holster_y = body.holster_y() if body.tracking(t) else 0.85
        else:
            anchor = aim_anchor = np.array([0.5 * aspect, 0.55])
            sw, holster_y = cfg.fallback_sw, 0.85
        chest_scale = sw / cfg.shoulder_width_m

        infos = self._hands(frame, chest_scale, holster_y)
        had_lock = self.gun_pos is not None
        # Decided once per frame, from what was known BEFORE this frame's hands were sorted out.
        hands_apart = not self.reload.together(t)
        gun = self._pick_gun(infos, t, hands_apart)
        off = next((h for h in infos if h is not gun), None)

        # Where the two hands are relative to each other. The hand model if it sees both,
        # otherwise the body model's wrists, which survive overlap and the frame edge better.
        relation = None
        if gun is not None and off is not None:
            relation = (off.center - gun.wrist) / sw
        elif body.visible:
            wl, wr = body.wrist("L"), body.wrist("R")
            if wl is not None and wr is not None:
                relation = (wr - wl) / sw
        self.reload.observe(t, relation)

        steady = False
        if gun is not None:
            dt = min(0.1, t - self.gun_seen_t) if had_lock else 0.0
            if not had_lock or self.gun_pos is None:
                if t - self.gun_seen_t > cfg.aim_recenter_after_s:
                    self.mapper.forget_center()     # hand was away a while: it will not come back to the same spot
                    self.ref_tip = None
                self._drop_gun_lock()               # start filters and triggers clean
            self._learn_gun_arm(gun)

            # How far the fingertip has moved, in real metres: image travel divided by the hand's OWN
            # image scale (the hand is 2-3x nearer the camera than the chest). Minus how far the chest
            # has moved, so stepping aside to dodge does not drag the aim.
            # Travel from a fixed reference point, not position in the frame: the scale estimate
            # wobbles a few percent as the hand turns, and a few percent of "40% across the image" is
            # centimetres of error. Seen live as the crosshair going wrong whenever it neared a corner.
            self.gun_scale.push(t, gun.scale)
            scale = self.gun_scale.median()
            tip = gun.p2[INDEX_TIP]
            if self.ref_tip is None:
                self.ref_tip, self.ref_chest = tip.copy(), aim_anchor.copy()
            raw = self.aim_filter((tip - self.ref_tip) / scale - (aim_anchor - self.ref_chest) / chest_scale, t)
            self.history.push(t, raw)

            if self.gun_center is not None and t > self.gun_seen_t:
                v = _dist(gun.center, self.gun_center) / (t - self.gun_seen_t) / scale
                self.hand_speed += (v - self.hand_speed) * 0.5
            self.gun_pos, self.gun_center, self.gun_seen_t = gun.wrist, gun.center, t

            # Where the player first points is "the middle of the screen". That is where the hand
            # comes to REST, not where it is first seen: seen live, the centre was taken while the
            # hand was still on its way up, and the first shots landed in the top corner. But do
            # not wait for stillness forever either: a recorded player started pumping shots at
            # once and never held still, so after aim_settle_max_s take what we have.
            if self.recenter_aim:
                self.mapper.center_on(raw)
                self.recenter_aim = False
            elif not self.mapper.centered:
                if self.lock_t is None:
                    self.lock_t = t
                if self.hand_speed > cfg.aim_settle_speed:
                    self.still_since, self.centering = None, []
                else:
                    self.centering.append(raw)
                    if self.still_since is None:
                        self.still_since = t
                if self.still_since is not None and t - self.still_since >= cfg.aim_settle_s:
                    self.mapper.center_on(np.median(np.array(self.centering), axis=0))
                elif t - self.lock_t >= cfg.aim_settle_max_s:
                    self.mapper.center_on(raw)
            # While the hands are together (a reload) the tracked hand may be the wrong one, so the
            # crosshair holds still. And only a hand moving at a human pace may push the mapping off
            # a screen edge: a tracking jump must not drag the centre away with it.
            if hands_apart:
                self.aim = self.mapper.map(raw, dt if self.hand_speed < cfg.aim_push_max_speed else 0.0)

            tip_rise = (gun.p2[WRIST][1] - gun.p2[INDEX_TIP][1]) / scale
            kick_onset = self.flick.update(t, tip_rise)
            if self.slap_kick.update(t, tip_rise) is not None and self.reload.together(t):
                self._reload(events, t)
            steady = self.hand_speed < cfg.thumb_max_speed and self.flick.swing(cfg.thumb_swing_s) < cfg.thumb_max_swing
            thumb_onset = self.thumb.update(t, thumb_feature(gun.hand), steady)

            if kick_onset is not None:
                if self.reload.together(t):
                    self._reload(events, t)
                elif cfg.flick_enabled:
                    if self.reload.maybe_together(t):
                        self.pending_kick = (t + cfg.slap_decide_s, self._rewound(t, kick_onset))
                    else:
                        self._fire(events, t, self._rewound(t, kick_onset), "flick")
            if thumb_onset is not None:
                self._fire(events, t, self._rewound(t, thumb_onset), "thumb")

        # The glove's switch. It sits under the thumb, so pressing it is also a thumb drop:
        # the lockout between different triggers makes that one shot. Like the gestures, the
        # press jolts the hand, so the shot goes where the aim was held just before it.
        for t_press in self.button_presses:
            if self.history.buf and t - self.gun_seen_t < 1.0:
                self._fire(events, t, self._rewound(t, t_press), "button")
        self.button_presses = []

        if off is not None:
            off_kick = self.off_flick.update(t, (off.p2[WRIST][1] - off.p2[INDEX_TIP][1]) / off.scale)
            if off_kick is not None and self.reload.together(t):
                self._reload(events, t)

        if self.pending_kick is not None:
            if self.reload.together(t):
                self._reload(events, t)
            elif t >= self.pending_kick[0]:
                raw_then, self.pending_kick = self.pending_kick[1], None
                self._fire(events, t, raw_then, "flick")

        scale_now = self.gun_scale.median() if self.gun_scale.buf else chest_scale
        if self.reload.approach(t, gun.wrist if gun else None, off.center if (gun and off) else None, scale_now, sw):
            self._reload(events, t)

        # Debounced flags.
        if gun is not None and not gun.open:
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

        # Holstered = the gun arm is down. The body model's wrist backs up the hand
        # model, so a hand-tracking dropout while aiming does not read as a holster.
        off_arm = _other(self.gun_arm) if self.gun_arm else None
        up = gun is not None or any(h.raised and h.arm != off_arm for h in infos)
        if not up and body.visible:
            for arm in ((self.gun_arm,) if self.gun_arm else ("L", "R")):
                w = body.wrist(arm, 0.6)
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
            "body_visible": body.visible, "gun_arm": self.gun_arm, "steady": steady,
            "rejected": len(frame.hands) - len(infos),
        }
        state = P.State(
            aim_x=self.aim[0], aim_y=self.aim[1],
            aim_valid=float(t - self.gun_seen_t <= cfg.aim_hold_s),
            gun_pose=float(self.gun_pose), holstered=float(holstered),
            lean=body.lean, duck=body.duck, body_speed=body.speed,
            tracking=float(tracking), off_hand_open=float(self.off_open),
        )
        return state, events
