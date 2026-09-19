"""Finger-gun tracker: webcam in, OSC out.

    python run.py                          # camera 0, send to Unreal on this machine
    python run.py --host 10.0.0.5          # Unreal runs on another machine
    python run.py --camera 1 --pose-every 2
    python run.py --record                 # save landmarks to recordings/ for offline tuning
    python run.py --replay recordings/x.jsonl --set thumb_fire_frac=0.55
"""
import argparse
import sys
import threading
import time
from pathlib import Path

import cv2

import protocol as P
from config import Config
from debug_view import DebugView
from glove import FIRE, RELOAD, Glove
from landmarks import Landmarker, frame_from_json, frame_to_json
from osc_io import OscIn, OscOut
from pipeline import Pipeline

CALIB_CORNERS = [(0.15, 0.2), (0.85, 0.2), (0.85, 0.8), (0.15, 0.8)]
WINDOW = "finger gun tracker"
LOW_FPS = 20


class Camera:
    """Grabs on its own thread so the tracker always gets the newest frame, never a queued one."""

    def __init__(self, index, width, height):
        backend = cv2.CAP_DSHOW if sys.platform == "win32" else cv2.CAP_ANY
        self.cap = cv2.VideoCapture(index, backend)
        if not self.cap.isOpened() and sys.platform == "darwin":
            # The first attempt makes macOS show its camera prompt, and that attempt always
            # fails. If we exit now the prompt vanishes with us, so wait for the answer.
            print("[camera] no access yet. If macOS is asking for camera permission, click Allow (waiting 25s).\n"
                  "[camera] No prompt? System Settings > Privacy & Security > Camera, turn on the app you launched this from.")
            deadline = time.monotonic() + 25
            while not self.cap.isOpened() and time.monotonic() < deadline:
                time.sleep(2.0)
                self.cap.release()
                self.cap = cv2.VideoCapture(index, backend)
        if sys.platform == "win32":
            self.cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))   # 720p30 needs MJPG on most webcams
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        self.cap.set(cv2.CAP_PROP_FPS, 30)
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        if not self.cap.isOpened():
            raise SystemExit(f"could not open camera {index}. Try --camera 1 and close other apps using it. On macOS: "
                             "System Settings > Privacy & Security > Camera, turn on the app you launched this from "
                             "(Terminal, iTerm, Claude, VS Code...), then run again.")
        self.lock = threading.Condition()
        self.frame = None
        self.seq = 0
        self.running = True
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def _loop(self):
        while self.running:
            ok, frame = self.cap.read()
            if not ok:
                time.sleep(0.01)
                continue
            with self.lock:
                self.frame, self.t, self.seq = frame, time.monotonic(), self.seq + 1
                self.lock.notify_all()

    def read(self, last_seq, timeout=2.0):
        with self.lock:
            if not self.lock.wait_for(lambda: self.seq != last_seq, timeout):
                return None, None, last_seq
            return self.frame, self.t, self.seq

    def close(self):
        self.running = False
        self.thread.join(timeout=1.0)
        self.cap.release()


class LocalCalibration:
    """The C key: run the same 4-target calibration the game drives over OSC, inside the tuning window."""

    def __init__(self):
        self.index = None

    def start(self, pipeline):
        self.index = 0
        pipeline.handle_command(P.ADDR_CALIB_BEGIN, ())
        pipeline.handle_command(P.ADDR_CALIB_TARGET, CALIB_CORNERS[0])

    def update(self, pipeline):
        if self.index is None or len(pipeline.mapper.pairs) <= self.index:
            return
        self.index += 1
        if self.index < len(CALIB_CORNERS):
            pipeline.handle_command(P.ADDR_CALIB_TARGET, CALIB_CORNERS[self.index])
        else:
            self.index = None
            print("[calib] done")


def replay_frames(path, realtime):
    start_wall, start_t = time.monotonic(), None
    with open(path) as f:
        for line in f:
            frame = frame_from_json(line)
            if start_t is None:
                start_t = frame.t
            if realtime:
                time.sleep(max(0.0, (frame.t - start_t) - (time.monotonic() - start_wall)))
            yield None, frame


def camera_frames(args, cfg):
    camera = Camera(args.camera, args.width, args.height)
    landmarker = Landmarker(cfg, pose_every=args.pose_every, delegate=args.delegate)
    seq = 0
    try:
        while True:
            bgr, t, seq = camera.read(seq)
            if bgr is None:
                print("[camera] no frames arriving")
                continue
            bgr = cv2.flip(bgr, 1)          # mirror: move right, crosshair goes right
            yield bgr, landmarker.process(bgr, t)
    finally:
        camera.close()
        landmarker.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1", help="machine running Unreal")
    ap.add_argument("--port", type=int, default=P.PORT_TO_GAME)
    ap.add_argument("--in-port", type=int, default=P.PORT_TO_TRACKER)
    ap.add_argument("--camera", type=int, default=0)
    ap.add_argument("--width", type=int, default=1280)
    ap.add_argument("--height", type=int, default=720)
    ap.add_argument("--pose-every", type=int, default=1, help="run the body model every Nth frame (2 saves CPU)")
    ap.add_argument("--delegate", choices=("cpu", "gpu"), default="cpu", help="gpu is experimental, see landmarks.py")
    ap.add_argument("--arduino", default="auto", help="glove serial port, e.g. /dev/cu.usbmodem1101 or COM5. auto = find it, off = no glove")
    ap.add_argument("--no-window", action="store_true")
    ap.add_argument("--windowed", action="store_true", help="tuning window not fullscreen (W toggles)")
    ap.add_argument("--frames", type=int, default=0, help="stop after this many frames (0 = run until Q)")
    ap.add_argument("--record", nargs="?", const="auto", help="write landmarks to a .jsonl file")
    ap.add_argument("--replay", help="run the pipeline over a recorded .jsonl instead of the camera")
    ap.add_argument("--realtime", action="store_true", help="replay at recorded speed")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE", help="override a config.py value")
    args = ap.parse_args()

    cfg = Config()
    cfg.apply_overrides(args.set)
    pipeline = Pipeline(cfg)
    out = OscOut(args.host, args.port)
    osc_in = OscIn(args.in_port)
    view = None if args.no_window else DebugView()
    fullscreen = not args.windowed
    if view:
        # Fullscreen by default: the crosshair is mapped to the whole screen, like the game's will be.
        # In a small window it would travel less than your finger points and feel wrong.
        cv2.namedWindow(WINDOW, cv2.WINDOW_NORMAL)
        if fullscreen:
            cv2.setWindowProperty(WINDOW, cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)
    calib = LocalCalibration()
    glove = None if (args.arduino == "off" or args.replay) else Glove(args.arduino)
    print(f"[tracker] sending to {args.host}:{args.port}, listening on {args.in_port}")

    recorder = None

    def start_recording(path=None):
        nonlocal recorder
        if path in (None, "auto"):
            Path("recordings").mkdir(exist_ok=True)
            path = f"recordings/{time.strftime('%Y%m%d-%H%M%S')}.jsonl"
        recorder = open(path, "w")
        print(f"[record] writing {path}")

    if args.record and not args.replay:
        start_recording(args.record)

    source = replay_frames(args.replay, args.realtime) if args.replay else camera_frames(args, cfg)
    counts = {"frames": 0, "fire": 0, "reload": 0, "hands": 0, "body": 0}
    fps, last_wall = 0.0, time.monotonic()
    low_fps_warned, pending_events = 0.0, []
    try:
        for bgr, frame in source:
            work_start = time.monotonic()
            for address, cmd_args in osc_in.poll():
                print(f"[osc in] {address} {cmd_args}")
                pipeline.handle_command(address, cmd_args)
            if glove:
                for t_press in glove.poll():
                    pipeline.press_button(t_press)
            state, events = pipeline.update(frame)
            calib.update(pipeline)
            out.state(state)
            for e in events:
                if e[0] == "fire":
                    out.fire(e[1], e[2])
                    print(f"[{frame.t:9.2f}] FIRE   ({e[1]:.3f}, {e[2]:.3f})  {e[3]}")
                    if glove and e[3] != "button":
                        glove.send(FIRE)        # the switch already buzzed by itself
                else:
                    out.reload()
                    print(f"[{frame.t:9.2f}] RELOAD")
                    if glove:
                        glove.send(RELOAD)
                counts[e[0]] += 1
            if recorder:
                recorder.write(frame_to_json(frame) + "\n")

            counts["frames"] += 1
            counts["hands"] += bool(frame.hands)
            counts["body"] += pipeline.body.visible
            now = time.monotonic()
            fps += (1.0 / max(1e-3, now - last_wall) - fps) * 0.1
            last_wall = now
            # Landmarks were computed before this loop body ran, so add their age for the true per-frame cost.
            ms = (now - frame.t) * 1000 if not args.replay else (now - work_start) * 1000

            # Every time-based rule in the detectors assumes ~30 frames a second. Seen live: on a
            # machine deep in swap the tracker fell to 3 fps and nothing it did meant anything.
            if counts["frames"] > 60 and fps < LOW_FPS and now - low_fps_warned > 5.0:
                low_fps_warned = now
                print(f"[tracker] LOW FRAME RATE: {fps:.0f} fps. Triggers and aim are unreliable below {LOW_FPS}. "
                      "Close other apps, or try --pose-every 2 / --no-window.")
            pending_events += events
            # Drawing the window is the one cost we can shed: when frames are slow, draw fewer of them.
            draw_every = 1 if fps >= 26 else 2 if fps >= LOW_FPS else 3
            if view and counts["frames"] % draw_every == 0:
                cv2.imshow(WINDOW, view.draw(bgr, frame, pipeline, state, pending_events, fps, ms, recorder is not None, low_fps=fps < LOW_FPS and counts["frames"] > 60, glove=glove.status if glove else "glove off"))
                pending_events = []
                key = cv2.waitKey(1) & 0xFF
                if key in (ord("q"), 27):
                    break
                elif key == ord("c"):
                    calib.start(pipeline)
                elif key == ord("n"):
                    pipeline.body.recenter()
                elif key == ord("x"):
                    pipeline.recenter_aim = True
                elif key in (ord("["), ord("]")):
                    cfg.aim_gain = round(max(0.2, min(3.0, cfg.aim_gain * (1.15 if key == ord("]") else 1 / 1.15))), 3)
                    print(f"[tracker] aim_gain = {cfg.aim_gain}   (keep it with: --set aim_gain={cfg.aim_gain})")
                elif key == ord("w"):
                    fullscreen = not fullscreen
                    cv2.setWindowProperty(WINDOW, cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN if fullscreen else cv2.WINDOW_NORMAL)
                elif key == ord("f"):
                    cfg.flick_enabled = not cfg.flick_enabled
                    print(f"[tracker] flick trigger {'on' if cfg.flick_enabled else 'off'}")
                elif key == ord("r") and not args.replay:
                    if recorder:
                        recorder.close()
                        recorder = None
                        print("[record] stopped")
                    else:
                        start_recording()
            if args.frames and counts["frames"] >= args.frames:
                break
    except KeyboardInterrupt:
        pass
    finally:
        source.close()          # releases the camera and the models
        if glove:
            glove.close()
        if recorder:
            recorder.close()
        osc_in.close()
        if view:
            cv2.destroyAllWindows()
    n = max(1, counts["frames"])
    print(f"[tracker] {counts['frames']} frames, {fps:.1f} fps, hands in {100 * counts['hands'] // n}% of frames, "
          f"body in {100 * counts['body'] // n}%, {counts['fire']} shots, {counts['reload']} reloads")


if __name__ == "__main__":
    main()
