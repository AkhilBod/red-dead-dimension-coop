"""Geometry on the 21 MediaPipe hand landmarks."""
import numpy as np

WRIST = 0
THUMB_TIP = 4
INDEX_MCP, INDEX_PIP, INDEX_TIP = 5, 6, 8
MIDDLE_MCP = 9
PINKY_MCP = 17
THUMB_CHAIN = (1, 2, 3, 4)
FINGER_CHAINS = ((5, 6, 7, 8), (9, 10, 11, 12), (13, 14, 15, 16), (17, 18, 19, 20))
PALM = (0, 5, 9, 13, 17)


def iso2d(pts, aspect):
    """Image coords scaled so one unit is one image height on both axes."""
    return pts[:, :2] * np.array([aspect, 1.0])


def points3d(hand, aspect):
    if hand.world is not None:
        return hand.world
    return hand.pts * np.array([aspect, 1.0, aspect])


def _extension(p, chain):
    a, b, c, d = (p[i] for i in chain)
    length = np.linalg.norm(b - a) + np.linalg.norm(c - b) + np.linalg.norm(d - c)
    return float(np.linalg.norm(d - a) / length) if length > 1e-9 else 0.0


def extensions(hand, aspect):
    """(thumb, index, middle, ring, pinky). 1.0 = straight, about 0.5 = fully curled."""
    p = points3d(hand, aspect)
    return (_extension(p, THUMB_CHAIN),) + tuple(_extension(p, c) for c in FINGER_CHAINS)


def is_gun_pose(ext, cfg):
    return ext[1] > cfg.gun_index_ext and float(np.mean(ext[2:])) < cfg.gun_others_ext


def is_open_palm(ext, cfg):
    return ext[0] > cfg.open_thumb_ext and min(ext[1:]) > cfg.open_finger_ext


def thumb_feature(hand, aspect):
    """Thumb tip to index knuckle/PIP, in palm lengths. High = cocked, low = hammer down."""
    p = points3d(hand, aspect)
    palm = np.linalg.norm(p[MIDDLE_MCP] - p[WRIST])
    if palm < 1e-9:
        return None
    d = np.linalg.norm(p[THUMB_TIP] - p[INDEX_MCP]) + np.linalg.norm(p[THUMB_TIP] - p[INDEX_PIP])
    return float(d / (2.0 * palm))
