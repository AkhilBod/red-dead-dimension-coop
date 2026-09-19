"""Every tunable lives here.

Units: sw = shoulder widths (scale-free, so distance from the camera does not
matter), H = image heights, s = seconds. Override any value from the command
line with:  python run.py --set thumb_fire_frac=0.55 --set flick_enabled=0
"""
from dataclasses import dataclass, fields


@dataclass
class Config:
    # Shape of the game screen. Keeps hand motion isotropic on screen.
    screen_aspect: float = 16 / 9

    # --- MediaPipe -----------------------------------------------------------
    hand_detect_conf: float = 0.5
    hand_track_conf: float = 0.5
    pose_detect_conf: float = 0.5
    pose_track_conf: float = 0.5

    # --- Aim -----------------------------------------------------------------
    # The aim point is the hand's POSITION relative to the chest, not the
    # finger's direction: a finger pointed at the camera is foreshortened to
    # almost nothing, so its direction is the noisiest thing we could measure.
    aim_tip_weight: float = 0.35    # 0 = index knuckle only, 1 = fingertip only
    aim_span_x: float = 1.4         # hand travel (sw) that crosses the full screen width
    aim_center_x: float = 0.55      # uncalibrated rest position, sw out from mid-chest on the gun side
    aim_center_y: float = 0.05      # sw, negative = above the shoulder line. Live test: hands rest about level with the shoulders
    aim_span_min: float = 0.9       # calibration may not make the gain twitchier than this
    aim_span_max: float = 2.6
    aim_min_cutoff: float = 1.2     # One Euro: lower = steadier at rest
    aim_beta: float = 3.0           # One Euro: higher = less lag in fast moves
    anchor_min_cutoff: float = 0.8
    anchor_beta: float = 2.0
    aim_hold_s: float = 0.25        # keep the crosshair alive through short dropouts
    gun_lock_radius: float = 0.7    # sw the gun hand may jump between frames and stay "the gun"
    gun_unlock_s: float = 0.4
    fallback_sw: float = 0.3        # H, assumed shoulder width when no body is tracked
    calib_points: int = 4

    # --- Hand shape ----------------------------------------------------------
    # Extension = fingertip-to-knuckle distance / finger length. 1 straight, ~0.5 curled.
    gun_index_ext: float = 0.80
    gun_others_ext: float = 0.75
    open_finger_ext: float = 0.85
    open_thumb_ext: float = 0.78
    pose_on_frames: int = 2
    pose_off_s: float = 0.3

    # --- Thumb-drop trigger --------------------------------------------------
    # Feature: thumb tip to index knuckle/PIP distance, in palm lengths.
    # "hi" tracks each player's own cocked-thumb level, thresholds are fractions of it.
    thumb_min_cocked: float = 0.5
    thumb_fire_frac: float = 0.62
    thumb_rearm_frac: float = 0.85
    thumb_drop_frac: float = 0.22   # must have fallen this much (x hi) inside the window
    thumb_window_s: float = 0.35
    thumb_confirm_frames: int = 2   # ignore one-frame landmark glitches
    thumb_hi_rise_s: float = 0.5     # slow enough that a few glitched frames cannot drag the level up
    thumb_hi_fall_s: float = 4.0

    # --- Recoil-flick trigger ------------------------------------------------
    # Feature: fingertip height above the wrist. A flick ROTATES the hand, so the
    # tip rises relative to the wrist. Re-aiming TRANSLATES the hand, so it does not.
    flick_enabled: bool = True
    flick_rise: float = 0.16        # sw gained within flick_window_s
    flick_window_s: float = 0.10
    flick_quiet: float = 0.07       # sw of change allowed in the quiet period before
    flick_quiet_s: float = 0.15
    flick_rearm: float = 0.04
    flick_confirm_frames: int = 2

    # --- Firing --------------------------------------------------------------
    fire_cooldown_s: float = 0.25
    rewind_margin_s: float = 0.03   # step a little further back than the detected onset
    rewind_max_s: float = 0.35
    trigger_gap_reset_s: float = 0.25
    fire_hold_radius: float = 1.1   # sw. Other hand this close to the gun wrist = the shot might be a slap
    fire_hold_s: float = 0.2        # so hold it this long and drop it if a reload arrives

    # --- Reload slap ---------------------------------------------------------
    slap_near: float = 0.55         # sw between off-hand palm and gun wrist
    slap_watch_radius: float = 1.0
    slap_approach: float = 0.18     # sw closed within slap_window_s
    slap_window_s: float = 0.12
    slap_above_tol: float = 0.15    # off hand may be this far above the gun wrist and still count
    slap_vanish_s: float = 0.2      # overlap makes a hand vanish: fast approach + vanish = contact
    slap_pose_near: float = 0.4
    slap_rearm: float = 0.8
    slap_cooldown_s: float = 0.5
    slap_lockout_s: float = 1.2
    slap_suppress_s: float = 0.4    # the slap knocks the gun hand up, which looks like a flick

    # --- Body ----------------------------------------------------------------
    sw_rise_s: float = 0.3          # shoulder-width envelope: turning sideways must not change the scale
    sw_fall_s: float = 8.0
    lean_full: float = 0.75         # sw of chest travel for lean = 1 (about a 30 cm sidestep)
    duck_full: float = 0.75         # sw of chest drop for duck = 1
    lean_deadzone: float = 0.06     # keeps the game camera still while the player just stands there
    duck_deadzone: float = 0.10
    lean_min_cutoff: float = 1.5
    lean_beta: float = 1.0
    settle_s: float = 0.6           # standing this still for this long sets the neutral stance
    settle_radius: float = 0.15     # sw
    baseline_forget_s: float = 3.0  # out of frame this long = a new player, learn the stance again
    neutral_drift_s: float = 30.0   # 0 disables
    stand_rise_s: float = 0.4
    stand_relax_s: float = 20.0
    body_hold_s: float = 1.0
    tracking_hold_s: float = 0.5
    speed_full: float = 2.5         # sw/s that reads as bodySpeed = 1
    holster_below_shoulder: float = 0.95   # sw under the shoulder line
    holster_delay_s: float = 0.25
    offhand_raise_line: float = 0.4        # sw under the shoulder line
    offhand_on_s: float = 0.15
    offhand_off_s: float = 0.3

    def apply_overrides(self, pairs):
        types = {f.name: f.type for f in fields(self)}
        for pair in pairs:
            key, _, raw = pair.partition("=")
            if key not in types:
                raise SystemExit(f"unknown config key '{key}'")
            current = getattr(self, key)
            if isinstance(current, bool):
                value = raw.lower() in ("1", "true", "yes", "on")
            else:
                value = type(current)(float(raw)) if isinstance(current, int) else float(raw)
            setattr(self, key, value)
