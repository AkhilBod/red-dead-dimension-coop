import unittest

import numpy as np

import synth
from synth import ASPECT, SHOULDER_Y, SW, ZOOM, Sim, make_hand, make_pose, rest_wrist

import protocol as P
from config import Config

CX = 0.5 * ASPECT


def aiming(wrist, thumb=0.0, pitch=0.0, center_x=CX, drop=0.0, extra_hands=(), left_wrist=None):
    wrists = {"R": tuple(wrist)}
    if left_wrist is not None:
        wrists["L"] = tuple(left_wrist)
    return [make_hand(wrist, thumb, pitch), *extra_hands], make_pose(center_x, drop, wrists)


def pull_trigger(sim, wrist, flinch=(0.0, 0.0)):
    """Thumb down over 0.1s while the hand flinches, hold, release."""
    flinched = wrist + np.array(flinch) * SW
    sim.run(0.1, lambda k: aiming(wrist + (flinched - wrist) * k, thumb=k))
    sim.run(0.2, lambda k: aiming(flinched, thumb=1.0))
    sim.run(0.15, lambda k: aiming(flinched + (wrist - flinched) * k, thumb=1.0 - k))


class PipelineTests(unittest.TestCase):
    def test_thumb_drop_fires_once_and_rewinds_aim(self):
        sim = Sim()
        wrist = rest_wrist()
        before = sim.run(1.0, lambda k: aiming(wrist))
        pull_trigger(sim, wrist, flinch=(0.0, 0.08))
        flinched = sim.state
        sim.run(0.5, lambda k: aiming(wrist))
        fires = [e for _, e in sim.events if e[0] == "fire"]
        self.assertEqual(len(fires), 1)
        self.assertAlmostEqual(fires[0][1], before.aim_x, delta=0.02)
        # The flinch (3 cm) moves the live crosshair a sixth of the screen. The shot must not follow it.
        self.assertAlmostEqual(fires[0][2], before.aim_y, delta=0.03)

    def test_second_shot_needs_the_thumb_to_come_back_up(self):
        sim = Sim()
        wrist = rest_wrist()
        sim.run(0.8, lambda k: aiming(wrist))
        sim.run(0.1, lambda k: aiming(wrist, thumb=k))
        sim.run(1.5, lambda k: aiming(wrist, thumb=1.0))
        self.assertEqual(sim.count("fire"), 1)
        for _ in range(3):
            sim.run(0.3, lambda k: aiming(wrist))
            pull_trigger(sim, wrist)
        self.assertEqual(sim.count("fire"), 4)

    def test_fast_reaim_does_not_fire(self):
        sim = Sim()
        a = rest_wrist()
        b = a + np.array([0.5, -0.6]) * SW
        sim.run(0.6, lambda k: aiming(a))
        sim.run(0.25, lambda k: aiming(a + (b - a) * k))
        sim.run(0.5, lambda k: aiming(b))
        sim.run(0.25, lambda k: aiming(b + (a - b) * k))
        sim.run(0.5, lambda k: aiming(a))
        self.assertEqual(sim.count("fire"), 0)

    def _flick(self, sim):
        wrist = rest_wrist()
        sim.run(0.8, lambda k: aiming(wrist))
        sim.run(0.08, lambda k: aiming(wrist, pitch=0.6 * k))
        sim.run(0.15, lambda k: aiming(wrist, pitch=0.6 * (1 - k)))
        sim.run(0.5, lambda k: aiming(wrist))

    def test_flick_fires_once(self):
        sim = Sim()
        self._flick(sim)
        self.assertEqual(sim.count("fire"), 1)

    def test_flick_can_be_disabled(self):
        sim = Sim(Config(flick_enabled=False))
        self._flick(sim)
        self.assertEqual(sim.count("fire"), 0)

    def test_slap_reloads_and_does_not_fire(self):
        sim = Sim()
        wrist = rest_wrist()
        sim.run(0.8, lambda k: aiming(wrist))
        start, end = wrist + np.array([0.0, 1.2]) * SW, wrist + np.array([0.0, 0.3]) * SW

        def approach(k):
            off = start + (end - start) * k
            return aiming(wrist, extra_hands=[make_hand(off - np.array([0.06, 0.0]), open_palm=True, label="Left")], left_wrist=off)
        sim.run(0.2, approach)
        # Contact: the off hand vanishes and the gun hand gets knocked upward.
        knocked = wrist + np.array([0.0, -0.15]) * SW
        sim.run(0.1, lambda k: aiming(knocked, pitch=0.4, left_wrist=end))
        sim.run(0.4, lambda k: aiming(wrist))
        self.assertEqual(sim.count("reload"), 1)
        self.assertEqual(sim.count("fire"), 0)

    def test_slap_seen_late_by_the_body_model_does_not_fire(self):
        # Seen live: FIRE, then RELOAD 0.07-0.16s later. The hand model never sees the slapping
        # hand and the body model's wrist lags the real one, so the knock at contact reads as a
        # recoil flick before the reload is recognised.
        sim = Sim()
        wrist = rest_wrist()
        sim.run(0.8, lambda k: aiming(wrist))
        start, end = wrist + np.array([0.1, 1.6]) * SW, wrist + np.array([0.0, 0.2]) * SW
        lag, travel = 0.15, 0.25

        def off_wrist(elapsed):             # where the body model THINKS the off hand is
            k = min(1.0, max(0.0, (elapsed - lag) / travel))
            return start + (end - start) * k
        t0 = sim.t
        sim.run(travel, lambda k: aiming(wrist, left_wrist=off_wrist(sim.t - t0)))
        knocked = wrist + np.array([0.0, -0.15]) * SW
        sim.run(0.1, lambda k: aiming(knocked, pitch=0.4, left_wrist=off_wrist(sim.t - t0)))
        sim.run(0.3, lambda k: aiming(wrist, left_wrist=off_wrist(sim.t - t0)))
        sim.run(0.4, lambda k: aiming(wrist, left_wrist=start))
        self.assertEqual(sim.count("reload"), 1)
        self.assertEqual(sim.count("fire"), 0)

    def test_shot_with_the_other_hand_nearby_still_fires(self):
        # Two-handed grip: the shot may be held briefly, but it must come out.
        sim = Sim()
        wrist = rest_wrist()
        support = wrist + np.array([0.0, 0.3]) * SW
        sim.run(0.8, lambda k: aiming(wrist, left_wrist=support))
        sim.run(0.1, lambda k: aiming(wrist, thumb=k, left_wrist=support))
        sim.run(0.5, lambda k: aiming(wrist, thumb=1.0, left_wrist=support))
        self.assertEqual(sim.count("fire"), 1)
        self.assertEqual(sim.count("reload"), 0)

    def test_lean_moves_the_view_but_not_the_aim(self):
        sim = Sim()
        before = sim.run(0.8, lambda k: aiming(rest_wrist()))
        shift = 0.9 * SW
        hand = lambda body_shift: rest_wrist() + np.array([body_shift * ZOOM, 0.0])     # the nearer hand moves further in the image
        sim.run(0.4, lambda k: aiming(hand(shift * k), center_x=CX + shift * k))
        after = sim.run(0.6, lambda k: aiming(hand(shift), center_x=CX + shift))
        self.assertGreater(after.lean, 0.9)
        self.assertAlmostEqual(after.aim_x, before.aim_x, delta=0.03)
        self.assertAlmostEqual(after.aim_y, before.aim_y, delta=0.03)
        self.assertEqual(sim.count("fire"), 0)
        left = sim.run(1.0, lambda k: aiming(hand(-shift), center_x=CX - shift))
        self.assertLess(left.lean, -0.9)

    def test_neutral_stance_is_learned_wherever_the_player_stands(self):
        sim = Sim()
        off = CX + 1.5 * SW                     # standing well off to one side of the camera
        still = sim.run(1.0, lambda k: aiming(rest_wrist(off), center_x=off))
        self.assertAlmostEqual(still.lean, 0.0, delta=0.05)
        leaned = sim.run(0.8, lambda k: aiming(rest_wrist(off - 0.9 * SW), center_x=off - 0.9 * SW))
        self.assertLess(leaned.lean, -0.9)

    def test_new_player_gets_a_new_neutral_stance(self):
        sim = Sim()
        sim.run(1.0, lambda k: aiming(rest_wrist()))
        sim.run(3.5, lambda k: ([], None))      # walks off
        off = CX - 1.2 * SW
        state = sim.run(1.2, lambda k: aiming(rest_wrist(off), center_x=off))
        self.assertAlmostEqual(state.lean, 0.0, delta=0.05)
        self.assertEqual(state.tracking, 1.0)

    def test_recenter_command(self):
        sim = Sim()
        sim.run(1.0, lambda k: aiming(rest_wrist()))
        shift = 0.6 * SW
        leaned = sim.run(0.8, lambda k: aiming(rest_wrist(CX + shift), center_x=CX + shift))
        self.assertGreater(leaned.lean, 0.5)
        sim.pipeline.handle_command(P.ADDR_RECENTER, ())
        centered = sim.run(0.6, lambda k: aiming(rest_wrist(CX + shift), center_x=CX + shift))
        self.assertAlmostEqual(centered.lean, 0.0, delta=0.05)

    def test_duck_and_stand(self):
        sim = Sim()
        sim.run(0.8, lambda k: aiming(rest_wrist()))
        drop = 0.9 * SW
        sim.run(0.3, lambda k: aiming(rest_wrist(drop=drop * k), drop=drop * k))
        ducked = sim.run(0.5, lambda k: aiming(rest_wrist(drop=drop), drop=drop))
        self.assertGreater(ducked.duck, 0.9)
        stood = sim.run(0.8, lambda k: aiming(rest_wrist()))
        self.assertLess(stood.duck, 0.1)
        self.assertEqual(sim.count("fire"), 0)

    def test_holster_and_draw(self):
        sim = Sim()
        up = sim.run(0.6, lambda k: aiming(rest_wrist()))
        self.assertEqual((up.holstered, up.aim_valid, up.gun_pose), (0.0, 1.0, 1.0))
        hip = np.array([CX + 0.6 * SW, SHOULDER_Y + 1.6 * SW])
        down = sim.run(0.7, lambda k: aiming(hip))
        self.assertEqual((down.holstered, down.aim_valid), (1.0, 0.0))
        drawn = sim.run(0.3, lambda k: aiming(rest_wrist()))
        self.assertEqual((drawn.holstered, drawn.aim_valid), (0.0, 1.0))

    def test_hand_dropout_while_aiming_is_not_a_holster(self):
        sim = Sim()
        wrist = rest_wrist()
        sim.run(0.6, lambda k: aiming(wrist))
        lost = sim.run(0.6, lambda k: ([], make_pose(CX, 0.0, {"R": tuple(wrist)})))
        self.assertEqual(lost.holstered, 0.0)
        self.assertEqual(lost.aim_valid, 0.0)

    def test_off_hand_open_palm(self):
        sim = Sim()
        wrist = rest_wrist()
        palm_wrist = np.array([CX - 0.9 * SW, SHOULDER_Y - 0.2 * SW])
        sim.run(0.5, lambda k: aiming(wrist))
        shown = sim.run(0.4, lambda k: aiming(wrist, extra_hands=[make_hand(palm_wrist, open_palm=True, label="Left")], left_wrist=palm_wrist))
        self.assertEqual(shown.off_hand_open, 1.0)
        self.assertEqual(shown.aim_valid, 1.0)
        gone = sim.run(0.6, lambda k: aiming(wrist))
        self.assertEqual(gone.off_hand_open, 0.0)
        self.assertEqual(sim.count("reload"), 0)

    def test_open_palm_alone_is_not_a_gun(self):
        sim = Sim()
        state = sim.run(0.5, lambda k: ([make_hand(rest_wrist(), open_palm=True)], make_pose(wrists={"R": tuple(rest_wrist())})))
        self.assertEqual((state.aim_valid, state.gun_pose), (0.0, 0.0))

    def test_calibration_maps_pointing_positions_to_targets(self):
        sim = Sim()
        base = rest_wrist()
        corners = [((0.15, 0.2), (-0.3, -0.18)), ((0.85, 0.2), (0.3, -0.18)), ((0.85, 0.8), (0.3, 0.18)), ((0.15, 0.8), (-0.3, 0.18))]
        sim.pipeline.handle_command(P.ADDR_CALIB_BEGIN, ())
        for target, offset in corners:
            sim.pipeline.handle_command(P.ADDR_CALIB_TARGET, target)
            wrist = base + np.array(offset) * SW
            sim.run(0.6, lambda k: aiming(wrist))
            pull_trigger(sim, wrist)
        self.assertEqual(sim.count("fire"), 4)
        self.assertTrue(sim.pipeline.mapper.calibrated)
        self.assertFalse(sim.pipeline.mapper.active)
        for target, offset in corners:
            wrist = base + np.array(offset) * SW
            state = sim.run(0.8, lambda k: aiming(wrist))
            self.assertAlmostEqual(state.aim_x, target[0], delta=0.04)
            self.assertAlmostEqual(state.aim_y, target[1], delta=0.04)

    def test_crosshair_starts_in_the_middle_wherever_the_hand_comes_up(self):
        for offset in ((0.0, 0.0), (0.9, 0.5), (-0.6, -0.4)):
            sim = Sim()
            wrist = rest_wrist() + np.array(offset) * SW
            state = sim.run(0.8, lambda k: aiming(wrist))
            self.assertAlmostEqual(state.aim_x, 0.5, delta=0.02)
            self.assertAlmostEqual(state.aim_y, 0.5, delta=0.02)

    def test_centre_is_where_the_hand_comes_to_rest_not_where_it_is_first_seen(self):
        sim = Sim()
        rest = rest_wrist()
        low = rest + np.array([0.1, 0.9]) * SW              # first seen just above the holster line, on its way up
        sim.run(0.3, lambda k: aiming(low + (rest - low) * k))
        state = sim.run(0.6, lambda k: aiming(rest))
        self.assertAlmostEqual(state.aim_x, 0.5, delta=0.03)
        self.assertAlmostEqual(state.aim_y, 0.5, delta=0.03)

    def test_a_hand_that_never_holds_still_still_gets_a_centre(self):
        sim = Sim()
        rest = rest_wrist()
        wobble = lambda k: aiming(rest + np.array([0.0, 0.25 * np.sin(k * 40)]) * SW)
        sim.run(1.2, wobble)
        self.assertTrue(sim.pipeline.mapper.centered)

    def test_a_flick_into_the_corner_and_back_returns_to_the_same_aim(self):
        sim = Sim()
        wrist = rest_wrist()
        before = sim.run(0.8, lambda k: aiming(wrist))
        far = wrist + np.array([1.2, -0.8]) * SW             # well past the top-right corner
        sim.run(0.15, lambda k: aiming(wrist + (far - wrist) * k))
        pinned = sim.run(0.15, lambda k: aiming(far))
        self.assertEqual((pinned.aim_x, pinned.aim_y), (1.0, 0.0))
        sim.run(0.15, lambda k: aiming(far + (wrist - far) * k))
        after = sim.run(0.6, lambda k: aiming(wrist))
        self.assertAlmostEqual(after.aim_x, before.aim_x, delta=0.1)
        self.assertAlmostEqual(after.aim_y, before.aim_y, delta=0.15)

    def test_a_hand_held_past_the_edge_gets_the_crosshair_back(self):
        sim = Sim()
        wrist = rest_wrist()
        sim.run(0.8, lambda k: aiming(wrist))
        far = wrist + np.array([1.2, 0.0]) * SW
        sim.run(0.4, lambda k: aiming(wrist + (far - wrist) * k))
        sim.run(3.0, lambda k: aiming(far))                  # player is simply standing further right than the centre assumed
        back = far - np.array([0.2, 0.0]) * SW
        state = sim.run(0.6, lambda k: aiming(far + (back - far) * k))
        self.assertLess(state.aim_x, 0.97)

    def test_recenter_command_centres_the_crosshair(self):
        sim = Sim()
        wrist = rest_wrist()
        sim.run(0.8, lambda k: aiming(wrist))
        moved = wrist + np.array([0.3, 0.2]) * SW
        state = sim.run(0.6, lambda k: aiming(moved))
        self.assertGreater(state.aim_x, 0.6)
        sim.pipeline.handle_command(P.ADDR_RECENTER, ())
        state = sim.run(0.4, lambda k: aiming(moved))
        self.assertAlmostEqual(state.aim_x, 0.5, delta=0.02)
        self.assertAlmostEqual(state.aim_y, 0.5, delta=0.02)

    def test_coming_back_in_a_new_posture_recentres_even_after_calibration(self):
        sim = Sim()
        base = rest_wrist()
        sim.pipeline.handle_command(P.ADDR_CALIB_BEGIN, ())
        for target, offset in (((0.15, 0.2), (-0.3, -0.18)), ((0.85, 0.2), (0.3, -0.18)), ((0.85, 0.8), (0.3, 0.18)), ((0.15, 0.8), (-0.3, 0.18))):
            sim.pipeline.handle_command(P.ADDR_CALIB_TARGET, target)
            sim.run(0.6, lambda k: aiming(base + np.array(offset) * SW))
            pull_trigger(sim, base + np.array(offset) * SW)
        self.assertTrue(sim.pipeline.mapper.calibrated)
        gain = sim.pipeline.mapper.k.copy()
        sim.run(3.0, lambda k: ([], make_pose()))               # hand down for a while
        elsewhere = base + np.array([1.2, 0.7]) * SW            # comes back held somewhere quite different
        state = sim.run(0.8, lambda k: aiming(elsewhere))
        self.assertAlmostEqual(state.aim_x, 0.5, delta=0.02)
        self.assertAlmostEqual(state.aim_y, 0.5, delta=0.02)
        np.testing.assert_allclose(sim.pipeline.mapper.k, gain)     # the calibrated sensitivity is kept

    def test_landmark_noise_causes_no_events_and_little_jitter(self):
        sim = Sim(noise=0.001)      # about 1.3 px at 720p, in line with what a recorded session showed
        wrist = rest_wrist()
        sim.run(1.0, lambda k: aiming(wrist))
        xs = []
        for _ in range(300):
            xs.append(sim.step(*aiming(wrist)).aim_x)
        self.assertEqual(sim.events, [])
        self.assertLess(float(np.std(xs)), 0.01)

    def test_no_body_falls_back_to_frame_position(self):
        sim = Sim()
        state = sim.run(0.6, lambda k: ([make_hand(rest_wrist())], None))
        self.assertEqual((state.aim_valid, state.tracking), (1.0, 0.0))
        pull_trigger_no_body = lambda k: ([make_hand(rest_wrist(), thumb=k)], None)
        sim.run(0.1, pull_trigger_no_body)
        sim.run(0.2, lambda k: ([make_hand(rest_wrist(), thumb=1.0)], None))
        self.assertEqual(sim.count("fire"), 1)


class GloveButtonTests(unittest.TestCase):
    def test_button_fires_where_the_aim_was_held_before_the_press(self):
        sim = Sim()
        wrist = rest_wrist()
        held = sim.run(1.0, lambda k: aiming(wrist))
        jolted = wrist + np.array([0.0, 0.1]) * SW          # pressing the switch jolts the hand down
        sim.pipeline.press_button(sim.t)
        sim.run(0.2, lambda k: aiming(jolted))
        fires = [e for _, e in sim.events if e[0] == "fire"]
        self.assertEqual(len(fires), 1)
        self.assertEqual(fires[0][3], "button")
        self.assertAlmostEqual(fires[0][1], held.aim_x, delta=0.02)
        self.assertAlmostEqual(fires[0][2], held.aim_y, delta=0.03)

    def test_button_shot_from_a_sweeping_hand_lands_on_the_crosshair_not_behind_it(self):
        # Seen live: click while tracking a moving target and the hit mark trailed the crosshair.
        sim = Sim()
        wrist = rest_wrist()
        sim.run(1.0, lambda k: aiming(wrist))
        sweep = np.array([0.6, 0.0]) * SW
        shown = sim.run(0.3, lambda k: aiming(wrist + sweep * k * 0.5))
        sim.pipeline.press_button(sim.t)
        sim.run(0.3, lambda k: aiming(wrist + sweep * (0.5 + k * 0.5)))
        fires = [e for _, e in sim.events if e[0] == "fire"]
        self.assertEqual(len(fires), 1)
        self.assertGreater(abs(shown.aim_x - 0.5), 0.1)                 # the sweep really moved the crosshair
        self.assertAlmostEqual(fires[0][1], shown.aim_x, delta=0.015)

    def test_button_under_the_thumb_is_one_shot_not_two(self):
        # The switch sits where the thumb lands, so pressing it is also a thumb-drop gesture.
        sim = Sim()
        wrist = rest_wrist()
        sim.run(1.0, lambda k: aiming(wrist))
        sim.run(0.1, lambda k: aiming(wrist, thumb=k))
        sim.pipeline.press_button(sim.t)
        sim.run(0.3, lambda k: aiming(wrist, thumb=1.0))
        self.assertEqual(sim.count("fire"), 1)

    def test_button_respects_the_fire_rate_and_needs_a_gun_hand(self):
        sim = Sim()
        sim.pipeline.press_button(sim.t)                     # nobody is aiming
        sim.run(0.5, lambda k: ([], make_pose()))
        self.assertEqual(sim.count("fire"), 0)
        wrist = rest_wrist()
        sim.run(1.0, lambda k: aiming(wrist))
        for _ in range(3):                                   # switch bounce / mashing inside one frame
            sim.pipeline.press_button(sim.t)
        sim.run(0.1, lambda k: aiming(wrist))
        self.assertEqual(sim.count("fire"), 1)


class RecordedSessionRegressions(unittest.TestCase):
    """Each of these is a bug found by replaying a recorded live session."""

    def test_background_object_detected_as_a_hand_never_becomes_the_gun(self):
        sim = Sim()
        wrist = rest_wrist()
        phantom = lambda: make_hand(np.array([0.95 * ASPECT, 0.5]), zoom=0.35, label="Left", score=0.99)
        sim.run(0.8, lambda k: ([make_hand(wrist), phantom()], make_pose(wrists={"R": tuple(wrist)})))
        aimed = sim.state
        # The real hand drops out for a second. The phantom is still there, with a higher score.
        lost = sim.run(1.0, lambda k: ([phantom()], make_pose(wrists={"R": tuple(wrist)})))
        self.assertEqual(lost.aim_valid, 0.0)
        self.assertAlmostEqual(lost.aim_x, aimed.aim_x, delta=0.02)      # did not jump to the frame edge
        self.assertEqual(sim.events, [])

    def test_one_hand_reported_twice_is_one_hand(self):
        sim = Sim()
        wrist = rest_wrist()
        twice = lambda k: ([make_hand(wrist), make_hand(wrist + 0.004, label="Left", score=0.77)], make_pose(wrists={"R": tuple(wrist)}))
        sim.run(1.0, twice)
        self.assertIsNone(sim.pipeline.debug["off"])
        self.assertEqual(sim.events, [])

    def test_crosshair_travel_follows_real_distance_not_image_distance(self):
        # Seated at a laptop the hand is ~2.6x nearer the camera than the chest, so it moves 2.6x
        # further in the image. Five real centimetres must be the same crosshair travel either way.
        cfg = Config()
        for zoom in (1.8, 2.6):
            sim = Sim()
            a = rest_wrist()
            b = a + np.array([0.05 * synth.M * zoom, 0.0])
            start = sim.run(0.8, lambda k: ([make_hand(a, zoom=zoom)], make_pose(wrists={"R": tuple(a)})))
            end = sim.run(0.8, lambda k: ([make_hand(b, zoom=zoom)], make_pose(wrists={"R": tuple(b)})))
            self.assertAlmostEqual(end.aim_x - start.aim_x, 0.05 / cfg.aim_span_m, delta=0.02)

    def test_aim_near_the_edge_of_the_camera_frame_survives_scale_wobble(self):
        # Seen live: the crosshair went wrong whenever it neared a corner. The hand's image scale is
        # an estimate that wobbles as the hand turns. Here the hand sits far from the image centre
        # and only its apparent size changes by 8%: the crosshair must barely notice.
        sim = Sim()
        corner = np.array([0.88 * ASPECT, 0.2])
        pose = lambda: make_pose(wrists={"R": tuple(corner)})
        before = sim.run(1.0, lambda k: ([make_hand(corner, zoom=2.4)], pose()))
        after = sim.run(1.0, lambda k: ([make_hand(corner, zoom=2.4 * 1.08)], pose()))
        self.assertAlmostEqual(after.aim_x, before.aim_x, delta=0.04)
        self.assertAlmostEqual(after.aim_y, before.aim_y, delta=0.04)

    def test_gun_lock_does_not_slide_onto_the_slapping_hand(self):
        sim = Sim()
        wrist = rest_wrist()
        sim.run(1.0, lambda k: aiming(wrist))
        aimed = sim.state
        # Slap: the other hand arrives underneath, then the GUN hand drops out of view and
        # only the slapping hand is left, right where the gun was.
        under = wrist + np.array([0.0, 0.25]) * SW
        sim.run(0.3, lambda k: aiming(wrist, extra_hands=[make_hand(under, label="Left")], left_wrist=under))
        sim.run(0.5, lambda k: ([make_hand(wrist, label="Left")], make_pose(wrists={"L": tuple(wrist), "R": tuple(wrist + np.array([0.0, 0.2]) * SW)})))
        # Hands part. The slapping hand stays up on the far side, the gun arm is still down.
        far = np.array([CX - 0.9 * SW, SHOULDER_Y - 0.1 * SW])
        parted = sim.run(0.6, lambda k: ([make_hand(far, label="Left")], make_pose(wrists={"L": tuple(far)})))
        self.assertEqual(parted.aim_valid, 0.0)             # not mirrored onto the other hand
        back = sim.run(0.4, lambda k: aiming(wrist, extra_hands=[make_hand(far, label="Left")], left_wrist=far))
        self.assertEqual(back.aim_valid, 1.0)
        self.assertAlmostEqual(back.aim_x, aimed.aim_x, delta=0.03)
        self.assertEqual(sim.count("fire"), 0)

    def test_slap_where_only_the_slapping_hand_is_visible(self):
        # Seated: the gun hand sinks below the frame and the camera only sees the other hand kick.
        sim = Sim()
        wrist = rest_wrist()
        sim.run(1.0, lambda k: aiming(wrist))
        low = np.array([CX + 0.1 * SW, SHOULDER_Y + 0.5 * SW])
        together = {"L": tuple(low), "R": tuple(low + np.array([0.15, 0.1]) * SW)}
        sim.run(0.5, lambda k: ([make_hand(low, label="Left")], make_pose(wrists=together)))
        sim.run(0.1, lambda k: ([make_hand(low, pitch=0.5 * k, label="Left")], make_pose(wrists=together)))
        sim.run(0.3, lambda k: ([make_hand(low, pitch=0.5 * (1 - k), label="Left")], make_pose(wrists=together)))
        self.assertEqual(sim.count("reload"), 1)
        self.assertEqual(sim.count("fire"), 0)

    def test_slow_relaxed_recoil_still_fires(self):
        # The recorded recoils took 0.25 s to rise, not the 0.1 s snap first assumed.
        sim = Sim()
        wrist = rest_wrist()
        sim.run(1.0, lambda k: aiming(wrist))
        up = wrist + np.array([0.0, -0.2]) * SW
        sim.run(0.25, lambda k: aiming(wrist + (up - wrist) * k, pitch=0.5 * k))
        sim.run(0.4, lambda k: aiming(up + (wrist - up) * k, pitch=0.5 * (1 - k)))
        sim.run(0.4, lambda k: aiming(wrist))
        self.assertEqual(sim.count("fire"), 1)


if __name__ == "__main__":
    unittest.main()
