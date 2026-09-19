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
    def test_default_center_depends_on_gun_side(self):
        cfg = Config()
        m = AimMapper(cfg)
        self.assertEqual(m.map(np.array([cfg.aim_center_x, cfg.aim_center_y]), 1), (0.5, 0.5))
        self.assertEqual(m.map(np.array([-cfg.aim_center_x, cfg.aim_center_y]), -1), (0.5, 0.5))

    def test_calibration_gain_is_clamped(self):
        cfg = Config()
        m = AimMapper(cfg)
        m.begin()
        # Player barely moved their hand between the left and right targets.
        for raw, target in (((0.50, -0.2), (0.15, 0.5)), ((0.55, -0.2), (0.85, 0.5)), ((0.52, -0.3), (0.5, 0.2))):
            m.set_target(*target)
            self.assertTrue(m.add_shot(np.array(raw)))
        self.assertAlmostEqual(m.gain[0], 1.0 / cfg.aim_span_min)

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
