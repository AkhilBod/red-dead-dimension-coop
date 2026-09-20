"""The OSC contract between the tracker and Unreal. See PLAN.md section 4.

Every argument is sent as float32 so Blueprint can read them all with one
"get floats" call.
"""
from dataclasses import dataclass, astuple

PORT_TO_GAME = 7000
PORT_TO_TRACKER = 7001

ADDR_STATE = "/fg/state"
ADDR_FIRE = "/fg/fire"
ADDR_RELOAD = "/fg/reload"
ADDR_CALIB_BEGIN = "/fg/calib/begin"
ADDR_CALIB_TARGET = "/fg/calib/target"
ADDR_RECENTER = "/fg/recenter"
ADDR_AIM_MODE = "/fg/aim_mode"        # [1.0] = pointer on the fingertip, [0.0] = travel from a learned centre


@dataclass
class State:
    """Field order is the wire order of /fg/state. Do not reorder."""
    aim_x: float = 0.5
    aim_y: float = 0.5
    aim_valid: float = 0.0
    gun_pose: float = 0.0
    holstered: float = 0.0
    lean: float = 0.0
    duck: float = 0.0
    body_speed: float = 0.0
    tracking: float = 0.0
    off_hand_open: float = 0.0

    def to_floats(self):
        return [float(v) for v in astuple(self)]


STATE_FIELDS = tuple(State.__dataclass_fields__)
