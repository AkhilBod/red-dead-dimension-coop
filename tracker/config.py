"""Every tunable lives here.

Units: m = real metres (image distance divided by the image scale at that depth),
sw = shoulder widths, H = image heights, s = seconds. Override any value from the
command line with:  python run.py --set thumb_drop_frac=0.35 --set flick_enabled=0

Values marked [rec] were set from a recorded live session, not guessed.
"""
from dataclasses import dataclass, fields


@dataclass
class Config:
    # Shape of the game screen. Keeps hand motion isotropic on screen.
    screen_aspect: float = 16 / 9
    shoulder_width_m: float = 0.37      # turns the shoulder span in the image into a depth scale

    # --- MediaPipe -----------------------------------------------------------
    hand_detect_conf: float = 0.5
    hand_track_conf: float = 0.5
    pose_detect_conf: float = 0.5
    pose_track_conf: float = 0.5

    # --- Which detections are really the player's hands ------------------------
    # [rec] A background object was reported as a 0.99-confidence hand a third the size
    # of the real one, and stole the gun lock whenever the real hand dropped out.
    hand_min_scale_ratio: float = 0.85  # a real hand is never much FARTHER than the player's chest
    hand_free_scale_ratio: float = 1.5  # clearly nearer than the chest: accept without an arm match
    hand_arm_match: float = 1.2         # else the wrist must sit within this many hand lengths of a body-model wrist
    hand_dedupe: float = 0.5            # two detections closer than this many hand lengths are one hand

    # --- Aim -----------------------------------------------------------------
    # [rec] Every direction-based model was tried on recorded sessions and failed: a ray along
    # the finger, wrist-to-tip and elbow-to-tip all landed tens of cm off screen and jittered
    # 1-4 cm, because a finger pointed at the camera is foreshortened to nothing. Fingertip
    # POSITION is steady to ~2 mm, so aim is position, with the gain real geometry would give:
    # a ray from the eyes through the fingertip moves (eye distance / arm reach) times as far on
    # the screen as the fingertip moves. About 16 cm of fingertip travel per laptop screen width.
    camera_hfov_deg: float = 60.0       # horizontal field of view of the webcam
    screen_width_m: float = 0.30        # physical width of the game screen. ~1.2 for a 55 inch TV
    finger_reach_m: float = 0.06        # the fingertip is this much nearer the camera than the palm
    aim_lever_min: float = 1.2
    aim_lever_max: float = 4.0
    aim_gain: float = 0.75              # x the geometric gain. 1.0 = crosshair travels as far as an eye-fingertip ray. [ and ] keys
    aim_settle_speed: float = 0.25      # m/s. Once the raised hand is slower than this...
    aim_settle_s: float = 0.2           # ...for this long, where it points is the middle of the screen
    aim_settle_max_s: float = 0.8       # never settles (player firing at once): use wherever it is by then
    aim_recenter_after_s: float = 2.0   # gun hand gone this long: learn the centre again when it comes back
    aim_push_max_speed: float = 1.2     # m/s. Faster than this is a tracking jump, not a hand pushing past the screen edge
    calib_gain_min: float = 0.4         # calibration may scale the geometric gain by this much, no more
    calib_gain_max: float = 2.5
    aim_min_cutoff: float = 1.0         # One Euro: lower = steadier at rest
    aim_beta: float = 3.0               # One Euro: higher = less lag in fast moves (speeds here are m/s on the screen plane)
    aim_hold_s: float = 0.25            # keep the crosshair alive through short dropouts
    hand_scale_window_s: float = 0.7    # median window for the hand's image scale
    gun_lock_jump: float = 1.5          # hand lengths the gun hand may jump between frames and stay "the gun"
    gun_unlock_s: float = 0.4
    hand_switch_s: float = 2.0          # the OTHER arm's hand must be up alone this long before it becomes the gun
    fallback_sw: float = 0.3            # H, assumed shoulder width when no body is tracked
    calib_points: int = 4

    # --- Hand shape ----------------------------------------------------------
    open_finger_ext: float = 0.85       # fingertip-to-knuckle distance / finger length, 1 = straight
    open_thumb_ext: float = 0.78
    open_visible_frac: float = 0.6      # and each finger must LOOK at least this long in the image
    pose_on_frames: int = 2
    pose_off_s: float = 0.3

    # --- Thumb-drop trigger --------------------------------------------------
    # Feature: thumb tip to index knuckle/PIP distance, in palm lengths.
    # [rec] up = 0.55-0.75, down = 0.25-0.35, noise about 0.04.
    thumb_min_cocked: float = 0.42      # a peak lower than this is just noise around "down"
    thumb_drop_frac: float = 0.30       # fire when it falls this far below the recent peak
    thumb_rearm_frac: float = 0.30      # re-arm when it climbs this far above the following trough
    thumb_window_s: float = 0.4
    thumb_confirm_frames: int = 2       # ignore one-frame landmark glitches
    thumb_max_speed: float = 0.35       # m/s. [rec] thumb landmarks are garbage while the hand moves
    thumb_max_swing: float = 0.025      # m of hand rotation within thumb_swing_s that still counts as steady
    thumb_swing_s: float = 0.2

    # --- Recoil-flick trigger ------------------------------------------------
    # Feature: fingertip height above the wrist. [rec] level = 0.03-0.05 m, kicked = 0.10-0.15 m,
    # and a relaxed recoil takes 0.2-0.3 s to get there, not the sharp 0.1 s snap first assumed.
    flick_enabled: bool = True
    flick_rise_m: float = 0.05
    flick_max_rise_s: float = 0.4       # slower than this is tilting to aim higher, not a kick
    flick_baseline_s: float = 0.5
    flick_rearm_frac: float = 0.5
    flick_confirm_frames: int = 2

    # --- Firing --------------------------------------------------------------
    fire_cooldown_s: float = 0.25
    other_trigger_lockout_s: float = 0.45   # thumb drop then recoil kick is ONE shot, not two
    rewind_margin_s: float = 0.04       # a gesture is only detectable once under way: skip back past its first frames...
    rewind_window_s: float = 0.15       # ...and take the median aim over this stretch, where the hand was still held on target
    rewind_max_s: float = 0.6
    trigger_gap_reset_s: float = 0.25

    # --- Reload slap ---------------------------------------------------------
    # [rec] offset between the two hands in sw: slapping = within about 0.3 sideways,
    # shooting one-handed = 0.9-1.7 apart sideways.
    slap_together_dx: float = 0.5
    slap_together_dy: float = 1.1
    slap_maybe_dx: float = 0.75         # close enough to wait a moment before calling a kick a shot
    slap_decide_s: float = 0.15
    slap_near_m: float = 0.16           # approach path: palm this close to the gun wrist...
    slap_watch_m: float = 0.30
    slap_closing_m: float = 0.06        # ...having closed this much within slap_window_s...
    slap_window_s: float = 0.12
    slap_above_tol: float = 0.15        # ...from level or below (sw)
    slap_vanish_s: float = 0.2          # overlap makes a hand vanish: fast approach + vanish = contact
    slap_cooldown_s: float = 0.45
    slap_suppress_s: float = 0.4        # no shots right after a reload

    # --- Body ----------------------------------------------------------------
    sw_window_s: float = 3.0            # shoulder width = 90th percentile over this window: turning
    sw_percentile: float = 90.0         # sideways briefly must not change the scale, leaning in must wear off
    anchor_min_cutoff: float = 0.8
    anchor_beta: float = 2.0
    lean_full: float = 0.75             # sw of chest travel for lean = 1 (about a 30 cm sidestep)
    duck_full: float = 0.75             # sw of chest drop for duck = 1
    lean_deadzone: float = 0.06         # keeps the game camera still while the player just stands there
    duck_deadzone: float = 0.10
    lean_min_cutoff: float = 1.5
    lean_beta: float = 1.0
    settle_s: float = 0.6               # standing this still for this long sets the neutral stance
    settle_radius: float = 0.15         # sw
    baseline_forget_s: float = 3.0      # out of frame this long = a new player, learn the stance again
    neutral_drift_s: float = 30.0       # 0 disables
    stand_rise_s: float = 0.4
    stand_relax_s: float = 20.0
    body_hold_s: float = 1.0
    tracking_hold_s: float = 0.5
    speed_full: float = 2.5             # sw/s that reads as bodySpeed = 1
    holster_below_shoulder: float = 0.95    # sw under the shoulder line
    holster_delay_s: float = 0.25
    offhand_raise_line: float = 0.4         # sw under the shoulder line
    offhand_on_s: float = 0.15
    offhand_off_s: float = 0.3

    def apply_overrides(self, pairs):
        names = {f.name for f in fields(self)}
        for pair in pairs:
            key, _, raw = pair.partition("=")
            if key not in names:
                raise SystemExit(f"unknown config key '{key}'")
            current = getattr(self, key)
            if isinstance(current, bool):
                value = raw.lower() in ("1", "true", "yes", "on")
            else:
                value = int(float(raw)) if isinstance(current, int) else float(raw)
            setattr(self, key, value)
