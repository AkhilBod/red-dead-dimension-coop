"""Geometry on the 21 MediaPipe hand landmarks."""
import numpy as np

WRIST = 0
THUMB_TIP = 4
INDEX_MCP, INDEX_PIP, INDEX_TIP = 5, 6, 8
MIDDLE_MCP = 9
THUMB_CHAIN = (1, 2, 3, 4)
FINGER_CHAINS = ((5, 6, 7, 8), (9, 10, 11, 12), (13, 14, 15, 16), (17, 18, 19, 20))
PALM = (0, 5, 9, 13, 17)

# Real lengths (metres) of the rigid palm bones, medians of MediaPipe's metric "world"
# landmarks over a recorded session. Only long bones: short ones are all noise. The knuckle
# line (5-17) uses the 90th percentile: the model squashes it when the hand faces the camera,
# which is exactly when it is the only bone seen side-on and sets the scale.
LONG_BONES = ((0, 5, 0.0965), (0, 9, 0.0916), (0, 13, 0.0887), (0, 17, 0.0765), (5, 17, 0.062))
PALM_LENGTH_M = 0.0916
HAND_LENGTH_M = 0.19


def iso2d(pts, aspect):
    """Image coords scaled so one unit is one image height (H) on both axes."""
    return pts[:, :2] * np.array([aspect, 1.0])


def image_scale(p2):
    """Image heights per real metre at this hand's depth.

    A bone seen side-on spans (true length x scale) in the image, and less when it
    points at the camera. The rigid palm always has some bone roughly side-on, so the
    largest ratio is the scale. This is what lets us measure hand travel in real
    centimetres whether the player sits at the laptop or stands across the room.
    """
    return max(float(np.linalg.norm(p2[i] - p2[j])) / length for i, j, length in LONG_BONES)


def _chain_length(p, chain):
    a, b, c, d = (p[i] for i in chain)
    return float(np.linalg.norm(b - a) + np.linalg.norm(c - b) + np.linalg.norm(d - c))


def is_open_palm(hand, p2, scale, cfg):
    """All five fingers straight AND visibly long in the image.

    The second half matters: for a fist pointed at the camera the model cannot see
    the curled fingers and reports them as straight. But they are foreshortened to
    almost nothing in the image, while an open palm shown to the camera is not.
    """
    w = hand.world
    if w is None:
        return False
    for chain in FINGER_CHAINS:
        length = _chain_length(w, chain)
        if length < 1e-6:
            return False
        if np.linalg.norm(w[chain[3]] - w[chain[0]]) / length < cfg.open_finger_ext:
            return False
        if np.linalg.norm(p2[chain[3]] - p2[chain[0]]) < cfg.open_visible_frac * length * scale:
            return False
    thumb = _chain_length(w, THUMB_CHAIN)
    return thumb > 1e-6 and np.linalg.norm(w[4] - w[1]) / thumb > cfg.open_thumb_ext


def thumb_feature(hand):
    """Thumb tip to index knuckle/PIP in palm lengths. High = cocked, low = hammer down.

    Uses the metric landmarks and a FIXED palm length. Dividing by the measured palm
    length blew up whenever the model's 3D palm collapsed (hand pointed at the camera).
    Returns None on those frames.
    """
    w = hand.world
    if w is None or np.linalg.norm(w[MIDDLE_MCP] - w[WRIST]) < 0.06:
        return None
    d = np.linalg.norm(w[THUMB_TIP] - w[INDEX_MCP]) + np.linalg.norm(w[THUMB_TIP] - w[INDEX_PIP])
    return float(d / (2.0 * PALM_LENGTH_M))
