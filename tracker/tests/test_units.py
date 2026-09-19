import unittest

import numpy as np

import synth  # noqa: F401  (puts the tracker folder on sys.path)

import protocol as P
from aim import AimMapper
from config import Config
from landmarks import Frame, frame_from_json, frame_to_json
from one_euro import OneEuro


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
        self.assertEqual(m.map(np.array([0.3, -0.2]), 2.0), (0.5, 0.5))

    def test_centre_gain_and_lever(self):
        cfg = Config()
        m = AimMapper(cfg)
        m.center_on(np.array([0.2, -0.1]))
        self.assertEqual(m.map(np.array([0.2, -0.1]), 2.0), (0.5, 0.5))
        x, _ = m.map(np.array([0.2 + 0.03, -0.1]), 2.0)
        self.assertAlmostEqual(x, 0.5 + 0.03 * 2.0 / cfg.screen_width_m)

    def test_edge_push_drags_the_centre(self):
        m = AimMapper(Config())
        m.center_on(np.array([0.0, 0.0]))
        self.assertEqual(m.map(np.array([1.0, 0.0]), 2.0)[0], 1.0)           # far past the right edge
        self.assertLess(m.map(np.array([0.98, 0.0]), 2.0)[0], 0.9)           # 2 cm back is already well on screen
        m.center_on(np.array([0.0, 0.0]))
        m.map(np.array([1.0, 0.0]), 2.0, push=False)
        self.assertEqual(m.map(np.array([0.98, 0.0]), 2.0)[0], 1.0)          # without the push it would still be lost

    def test_calibration_gain_is_clamped(self):
        cfg = Config()
        m = AimMapper(cfg)
        m.begin()
        # Player barely moved their hand between the left and right targets.
        for raw, target in (((0.500, -0.2), (0.15, 0.5)), ((0.505, -0.2), (0.85, 0.5)), ((0.502, -0.3), (0.5, 0.2))):
            m.set_target(*target)
            self.assertTrue(m.add_shot(np.array(raw), 2.0))
        self.assertAlmostEqual(m.k[0], cfg.calib_gain_max)

    def test_shots_without_a_pending_target_are_ignored(self):
        m = AimMapper(Config())
        self.assertFalse(m.add_shot(np.array([0.0, 0.0]), 2.0))
        m.begin()
        self.assertFalse(m.add_shot(np.array([0.0, 0.0]), 2.0))
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
