import unittest

import numpy as np

import synth  # noqa: F401  (puts the tracker folder on sys.path)

import protocol as P
from aim import AimHistory, AimMapper
from config import Config
from landmarks import Frame, frame_from_json, frame_to_json
from one_euro import OneEuro


class AimHistoryTests(unittest.TestCase):
    def test_held_aim_rewinds_to_the_median_and_a_sweep_to_the_line(self):
        held, sweep = AimHistory(), AimHistory()
        for i in range(12):
            t = i / 30
            held.push(t, [0.10 + (0.03 if i == 8 else 0.0), 0.0])      # one glitch frame
            sweep.push(t, [0.5 * t, 0.0])                                # 0.5 m/s
        self.assertAlmostEqual(held.settled_before(0.36, 0.15, 0.12, read_at=0.40)[0], 0.10, places=6)
        self.assertAlmostEqual(sweep.settled_before(0.36, 0.15, 0.12, read_at=0.40)[0], 0.20, places=6)


class OneEuroTests(unittest.TestCase):
    def test_smooths_jitter_at_rest(self):
        rng = np.random.default_rng(0)
        f = OneEuro(1.2, 3.0)
        out = [f([0.5 + rng.normal(0, 0.01)], i / 30)[0] for i in range(300)]
        self.assertLess(np.std(out[60:]), 0.004)

    def test_follows_fast_motion_with_little_lag(self):
        f = OneEuro(1.2, 3.0)
        for i in range(30):
            f([0.0], i / 30)
        for i in range(30, 36):                 # 0.2s sweep across one unit
            out = f([(i - 29) / 6], i / 30)[0]
        self.assertGreater(out, 0.8)


class AimMapperTests(unittest.TestCase):
    def test_uncentred_maps_to_the_middle(self):
        m = AimMapper(Config())
        self.assertEqual(m.map(np.array([0.3, -0.2])), (0.5, 0.5))

    def test_a_fixed_physical_sensitivity(self):
        cfg = Config()
        m = AimMapper(cfg)
        m.center_on(np.array([0.2, -0.1]))
        self.assertEqual(m.map(np.array([0.2, -0.1])), (0.5, 0.5))
        x, y = m.map(np.array([0.2 + 0.09, -0.1 + 0.05]))
        self.assertAlmostEqual(x, 0.5 + 0.09 / cfg.aim_span_m)
        self.assertAlmostEqual(y, 0.5 + 0.05 / (cfg.aim_span_m / cfg.screen_aspect))

    def test_flicking_into_a_corner_and_back_leaves_the_mapping_alone(self):
        m = AimMapper(Config())
        m.center_on(np.array([0.0, 0.0]))
        for _ in range(6):                                          # 0.2 s well past the top-right corner
            self.assertEqual(m.map(np.array([0.5, -0.5]), dt=1 / 30), (1.0, 0.0))
        x, y = m.map(np.array([0.0, 0.0]), dt=1 / 30)
        self.assertAlmostEqual(x, 0.5, delta=0.12)
        self.assertAlmostEqual(y, 0.5, delta=0.2)

    def test_holding_past_an_edge_pulls_the_centre_along(self):
        m = AimMapper(Config())
        m.center_on(np.array([0.0, 0.0]))
        for _ in range(90):                                         # 3 s held way out to the right
            m.map(np.array([0.6, 0.0]), dt=1 / 30)
        self.assertLess(m.map(np.array([0.55, 0.0]))[0], 0.9)       # a small move back is already on screen again

    def test_reading_does_not_move_the_centre(self):
        m = AimMapper(Config())
        m.center_on(np.array([0.0, 0.0]))
        for _ in range(90):
            m.map(np.array([0.6, 0.0]))                             # dt = 0: a shot looking up its aim, not a frame
        np.testing.assert_allclose(m.center, [0.0, 0.0])

    def test_calibration_gain_is_clamped(self):
        cfg = Config()
        m = AimMapper(cfg)
        m.begin()
        # Player barely moved their hand between the left and right targets.
        for raw, target in (((0.500, -0.2), (0.15, 0.5)), ((0.505, -0.2), (0.85, 0.5)), ((0.502, -0.3), (0.5, 0.2))):
            m.set_target(*target)
            self.assertTrue(m.add_shot(np.array(raw)))
        self.assertAlmostEqual(m.k[0], cfg.calib_gain_max)

    def test_shots_without_a_pending_target_are_ignored(self):
        m = AimMapper(Config())
        self.assertFalse(m.add_shot(np.array([0.0, 0.0])))
        m.begin()
        self.assertFalse(m.add_shot(np.array([0.0, 0.0])))
        self.assertFalse(m.calibrated)


class ProtocolTests(unittest.TestCase):
    def test_state_wire_order(self):
        self.assertEqual(P.STATE_FIELDS, ("aim_x", "aim_y", "aim_valid", "gun_pose", "holstered", "lean", "duck", "body_speed", "tracking", "off_hand_open"))
        floats = P.State(aim_x=1, tracking=True).to_floats()
        self.assertTrue(all(type(v) is float for v in floats))

    def test_frame_record_round_trip(self):
        hands, pose = [synth.make_hand(synth.rest_wrist())], synth.make_pose()
        frame = Frame(1.25, synth.ASPECT, hands, pose)
        back = frame_from_json(frame_to_json(frame))
        self.assertEqual(back.t, 1.25)
        np.testing.assert_allclose(back.hands[0].pts, hands[0].pts, atol=1e-5)
        np.testing.assert_allclose(back.hands[0].world, hands[0].world, atol=1e-5)
        np.testing.assert_allclose(back.pose.vis, pose.vis, atol=1e-5)


if __name__ == "__main__":
    unittest.main()
