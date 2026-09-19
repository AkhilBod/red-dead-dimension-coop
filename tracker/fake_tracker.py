"""Stand-in for the real tracker: sends the exact same OSC from mouse and keys.

Use it to build and test the Unreal side with no camera.

    python fake_tracker.py                 # window, mouse + keys
    python fake_tracker.py --auto          # no window, scripted motion forever
    python fake_tracker.py --host 10.0.0.5 # Unreal on another machine
"""
import argparse
import math
import time

import cv2
import numpy as np

import protocol as P
from osc_io import OscIn, OscOut

W, H = 960, 540
RATE_HZ = 30
HELP = [
    "mouse = aim      left click = fire      R = reload",
    "A / D = lean left / right (press again or W to center)",
    "S = duck toggle  H = holster toggle     F = off-hand open toggle",
    "G = gun pose toggle   T = tracking toggle   Q = quit",
]


def approach(value, target, max_step):
    return value + max(-max_step, min(max_step, target - value))


def run_auto(out, seconds):
    print(f"[fake] auto mode -> {out.host}:{out.port}")
    start = time.monotonic()
    shots = 0
    next_fire = 1.0
    while seconds <= 0 or time.monotonic() - start < seconds:
        t = time.monotonic() - start
        ducking = (t % 8.0) > 6.5
        state = P.State(
            aim_x=0.5 + 0.4 * math.sin(t * 0.9),
            aim_y=0.5 + 0.3 * math.sin(t * 1.3),
            aim_valid=1, gun_pose=1, tracking=1,
            lean=math.sin(t * 0.6),
            duck=1.0 if ducking else 0.0,
            body_speed=abs(math.cos(t * 0.6)) * 0.6,
        )
        out.state(state)
        if t >= next_fire:
            next_fire += 1.0
            if shots == 6:
                out.reload()
                shots = 0
                print(f"[fake] {t:6.1f}s reload")
            else:
                out.fire(state.aim_x, state.aim_y)
                shots += 1
                print(f"[fake] {t:6.1f}s fire ({state.aim_x:.2f}, {state.aim_y:.2f})")
        time.sleep(1.0 / RATE_HZ)


def run_window(out, osc_in):
    state = P.State(aim_valid=1, gun_pose=1, tracking=1)
    target = {"lean": 0.0, "duck": 0.0}
    mouse = {"x": 0.5, "y": 0.5, "fire": False}
    flash = {"text": "", "until": 0.0}
    last_cmd = "none yet"

    def on_mouse(event, x, y, flags, _):
        mouse["x"] = min(1.0, max(0.0, x / W))
        mouse["y"] = min(1.0, max(0.0, y / H))
        if event == cv2.EVENT_LBUTTONDOWN:
            mouse["fire"] = True

    win = "fake tracker (click here, then use mouse + keys)"
    cv2.namedWindow(win)
    cv2.setMouseCallback(win, on_mouse)
    dt = 1.0 / RATE_HZ
    next_tick = time.monotonic()

    while True:
        key = cv2.waitKey(5) & 0xFF
        if key in (ord("q"), 27):
            break
        if key == ord("a"):
            target["lean"] = 0.0 if target["lean"] < 0 else -1.0
        elif key == ord("d"):
            target["lean"] = 0.0 if target["lean"] > 0 else 1.0
        elif key == ord("w"):
            target["lean"] = 0.0
        elif key == ord("s"):
            target["duck"] = 0.0 if target["duck"] > 0 else 1.0
        elif key == ord("h"):
            state.holstered = 0.0 if state.holstered else 1.0
        elif key == ord("f"):
            state.off_hand_open = 0.0 if state.off_hand_open else 1.0
        elif key == ord("g"):
            state.gun_pose = 0.0 if state.gun_pose else 1.0
        elif key == ord("t"):
            state.tracking = 0.0 if state.tracking else 1.0
        elif key == ord("r"):
            out.reload()
            flash.update(text="RELOAD", until=time.monotonic() + 0.3)

        now = time.monotonic()
        if now < next_tick:
            continue
        next_tick = max(next_tick + dt, now)

        prev = (state.lean, state.duck)
        state.lean = approach(state.lean, target["lean"], dt / 0.15)
        state.duck = approach(state.duck, target["duck"], dt / 0.2)
        state.body_speed = min(1.0, (abs(state.lean - prev[0]) + abs(state.duck - prev[1])) / dt / 5.0)
        state.aim_x, state.aim_y = mouse["x"], mouse["y"]
        hand_up = state.tracking and not state.holstered
        state.aim_valid = 1.0 if hand_up else 0.0
        out.state(state)

        if mouse["fire"]:
            mouse["fire"] = False
            if hand_up:
                out.fire(state.aim_x, state.aim_y)
                flash.update(text="FIRE", until=now + 0.15)

        for addr, args in osc_in.poll():
            last_cmd = f"{addr} {[round(a, 3) for a in args]}"
            print(f"[fake] from game: {last_cmd}")

        img = np.full((H, W, 3), 28, np.uint8)
        for i, line in enumerate(HELP):
            cv2.putText(img, line, (16, 28 + 24 * i), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (170, 170, 170), 1, cv2.LINE_AA)
        for i, (name, value) in enumerate(zip(P.STATE_FIELDS, state.to_floats())):
            cv2.putText(img, f"{name:>13} {value:+.2f}", (16, 170 + 22 * i), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (120, 220, 255), 1, cv2.LINE_AA)
        cv2.putText(img, f"last from game: {last_cmd}", (16, H - 16), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (150, 150, 150), 1, cv2.LINE_AA)
        cx, cy = int(state.aim_x * W), int(state.aim_y * H)
        color = (80, 255, 80) if hand_up else (80, 80, 80)
        cv2.circle(img, (cx, cy), 14, color, 2, cv2.LINE_AA)
        cv2.line(img, (cx - 22, cy), (cx + 22, cy), color, 1, cv2.LINE_AA)
        cv2.line(img, (cx, cy - 22), (cx, cy + 22), color, 1, cv2.LINE_AA)
        if now < flash["until"]:
            cv2.putText(img, flash["text"], (W // 2 - 90, H // 2), cv2.FONT_HERSHEY_DUPLEX, 1.6, (60, 200, 255), 3, cv2.LINE_AA)
        cv2.imshow(win, img)

    cv2.destroyAllWindows()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1", help="machine running Unreal")
    ap.add_argument("--port", type=int, default=P.PORT_TO_GAME)
    ap.add_argument("--in-port", type=int, default=P.PORT_TO_TRACKER)
    ap.add_argument("--auto", action="store_true", help="scripted motion, no window")
    ap.add_argument("--seconds", type=float, default=0, help="auto mode: stop after this long (0 = forever)")
    args = ap.parse_args()

    out = OscOut(args.host, args.port)
    if args.auto:
        run_auto(out, args.seconds)
        return
    osc_in = OscIn(args.in_port)
    try:
        run_window(out, osc_in)
    finally:
        osc_in.close()


if __name__ == "__main__":
    main()
