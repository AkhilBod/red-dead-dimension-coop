"""Finger-gun tracker: webcam in, OSC out.

    python run.py                          # camera 0, send to Unreal on this machine
    python run.py --host 10.0.0.5          # Unreal runs on another machine
    python run.py --no-window              # no tuning window: what the demo machine should run
    python run.py --record                 # save landmarks to recordings/ for offline tuning
    python run.py --replay recordings/x.jsonl --set thumb_drop_frac=0.35
"""
import argparse
import queue
import sys
import threading
import time
from pathlib import Path

import cv2

import protocol as P
from config import Config
from debug_view import DebugView, capture
from glove import FIRE, RELOAD, Glove
from landmarks import Landmarker, frame_from_json, frame_to_json
from osc_io import OscIn, OscOut
from pipeline import Pipeline

CALIB_CORNERS = [(0.15, 0.2), (0.85, 0.2), (0.85, 0.8), (0.15, 0.8)]
WINDOW = "finger gun tracker"
LOW_FPS = 20


class Camera:
    """Grabs on its own thread so the tracker always gets the newest frame, never a queued one."""

    def __init__(self, index, width, height, fps=60):
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
        self.cap.set(cv2.CAP_PROP_FPS, fps)
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        if not self.cap.isOpened():
            raise SystemExit(f"could not open camera {index}. Try --camera 1 and close other apps using it. On macOS: "
                             "System Settings > Privacy & Security > Camera, turn on the app you launched this from "
                             "(Terminal, iTerm, Claude, VS Code...), then run again.")
        print(f"[camera] {int(self.cap.get(cv2.CAP_PROP_FRAME_WIDTH))}x{int(self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT))} "
              f"at {self.cap.get(cv2.CAP_PROP_FPS):.0f} fps (asked for {fps})")
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


class Tracker(threading.Thread):
    """Camera -> landmarks -> pipeline -> OSC and glove, as fast as frames arrive.

    On its own thread because showing a window costs 10-25 ms a frame and must happen on the
    main thread on macOS. Measured live, that alone pulled the tracker from 30 to under 20 fps.
    Now the window draws whatever the newest result is, whenever it gets round to it, and the
    game's input does not care. Keys reach the pipeline through a queue for the same reason.
    """

    def __init__(self, args, cfg, camera):
        super().__init__(daemon=True)
        self.args, self.cfg, self.camera = args, cfg, camera
        self.pipeline = Pipeline(cfg)
        self.out = OscOut(args.host, args.port)
        self.osc_in = OscIn(args.in_port)
        self.glove = None if (args.arduino == "off" or args.replay) else Glove(args.arduino)
        self.calib = LocalCalibration()
        self.keys = queue.SimpleQueue()
        self.stop_flag = threading.Event()
        self.want_view = not args.no_window
        self.recorder = None
        self.fps = 0.0
        self.ms = 0.0
        self._lock = threading.Lock()
        self._latest = None
        self._events = []
        self.counts = {"frames": 0, "fire": 0, "reload": 0, "hands": 0, "body": 0}

    # ---- called from the window's thread ----

    def newest(self):
        """(frame image, view model, events since last asked) or None if nothing new."""
        with self._lock:
            if self._latest is None:
                return None
            (bgr, model), self._latest = self._latest, None
            events, self._events = self._events, []
            return bgr, model, events

    # ---- tracker thread ----

    def _frames(self):
        if self.args.replay:
            start_wall, start_t = time.monotonic(), None
            with open(self.args.replay) as f:
                for line in f:
                    frame = frame_from_json(line)
                    if start_t is None:
                        start_t = frame.t
                    if self.args.realtime:
                        time.sleep(max(0.0, (frame.t - start_t) - (time.monotonic() - start_wall)))
                    yield None, frame
            return
        landmarker = Landmarker(self.cfg, pose_every=self.args.pose_every, delegate=self.args.delegate)
        seq = 0
        try:
            while not self.stop_flag.is_set():
                bgr, t, seq = self.camera.read(seq)
                if bgr is None:
                    print("[camera] no frames arriving")
                    continue
                bgr = cv2.flip(bgr, 1)          # mirror: move right, crosshair goes right
                yield bgr, landmarker.process(bgr, t)
        finally:
            landmarker.close()

    def _record(self, path=None):
        if self.recorder:
            self.recorder.close()
            self.recorder = None
            print("[record] stopped")
            return
        if path in (None, "auto"):
            Path("recordings").mkdir(exist_ok=True)
            path = f"recordings/{time.strftime('%Y%m%d-%H%M%S')}.jsonl"
        self.recorder = open(path, "w")
        print(f"[record] writing {path}")

    def _handle_key(self, key):
        cfg, pipeline = self.cfg, self.pipeline
        if key == "c":
            self.calib.start(pipeline)
        elif key == "n":
            pipeline.body.recenter()
        elif key == "x":
            pipeline.recenter_aim = True
        elif key in "[]":
            cfg.aim_span_m = round(max(0.12, min(0.9, cfg.aim_span_m * (1 / 1.12 if key == "]" else 1.12))), 3)
            print(f"[tracker] {cfg.aim_span_m * 100:.0f} cm of fingertip travel crosses the screen   (keep it with: --set aim_span_m={cfg.aim_span_m})")
        elif key == "f":
            cfg.flick_enabled = not cfg.flick_enabled
            print(f"[tracker] recoil trigger {'on' if cfg.flick_enabled else 'off'}")
        elif key == "r" and not self.args.replay:
            self._record()

    def run(self):
        args, pipeline, glove = self.args, self.pipeline, self.glove
        print(f"[tracker] sending to {args.host}:{args.port}, listening on {args.in_port}")
        if args.record and not args.replay:
            self._record(args.record)
        last_wall, low_fps_warned = time.monotonic(), -1e9
        try:
            for bgr, frame in self._frames():
                work_start = time.monotonic()
                while True:
                    try:
                        self._handle_key(self.keys.get_nowait())
                    except queue.Empty:
                        break
                for address, cmd_args in self.osc_in.poll():
                    print(f"[osc in] {address} {cmd_args}")
                    pipeline.handle_command(address, cmd_args)
                if glove:
                    for t_press in glove.poll():
                        pipeline.press_button(t_press)
                state, events = pipeline.update(frame)
                self.calib.update(pipeline)
                self.out.state(state)
                for e in events:
                    if e[0] == "fire":
                        self.out.fire(e[1], e[2])
                        print(f"[{frame.t:9.2f}] FIRE   ({e[1]:.3f}, {e[2]:.3f})  {e[3]}")
                        if glove and e[3] != "button":
                            glove.send(FIRE)        # the switch already buzzed by itself
                    else:
                        self.out.reload()
                        print(f"[{frame.t:9.2f}] RELOAD")
                        if glove:
                            glove.send(RELOAD)
                    self.counts[e[0]] += 1
                if self.recorder:
                    self.recorder.write(frame_to_json(frame) + "\n")

                c = self.counts
                c["frames"] += 1
                c["hands"] += bool(frame.hands)
                c["body"] += pipeline.body.visible
                now = time.monotonic()
                self.fps += (1.0 / max(1e-3, now - last_wall) - self.fps) * 0.1
                last_wall = now
                # Camera frame to OSC packet. In a replay there is no camera, so it is just our own work.
                self.ms = (now - frame.t) * 1000 if not args.replay else (now - work_start) * 1000

                # Every time-based rule in the detectors assumes ~30 frames a second.
                if not args.replay and c["frames"] > 90 and self.fps < LOW_FPS and now - low_fps_warned > 30.0:
                    low_fps_warned = now
                    print(f"[tracker] LOW FRAME RATE: {self.fps:.0f} fps. Below {LOW_FPS} the triggers and aim get unreliable. Close other apps.")

                if self.want_view:
                    model = capture(pipeline, frame, state)
                    with self._lock:
                        self._latest = (bgr, model)
                        self._events += events
                if args.frames and c["frames"] >= args.frames:
                    break
        finally:
            if self.recorder:
                self.recorder.close()
            if glove:
                glove.close()
            self.osc_in.close()
            c, n = self.counts, max(1, self.counts["frames"])
            print(f"[tracker] {c['frames']} frames, {self.fps:.1f} fps, hands in {100 * c['hands'] // n}% of frames, "
                  f"body in {100 * c['body'] // n}%, {c['fire']} shots, {c['reload']} reloads")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1", help="machine running Unreal")
    ap.add_argument("--port", type=int, default=P.PORT_TO_GAME)
    ap.add_argument("--in-port", type=int, default=P.PORT_TO_TRACKER)
    ap.add_argument("--camera", type=int, default=0)
    ap.add_argument("--width", type=int, default=1280)
    ap.add_argument("--height", type=int, default=720)
    ap.add_argument("--fps", type=int, default=60, help="camera frame rate to ask for. More = fresher frames = less lag. "
                    "Cameras that cannot do it just give what they have")
    ap.add_argument("--pose-every", type=int, default=1, help="run the body model at most every Nth frame")
    ap.add_argument("--delegate", choices=("cpu", "gpu"), default="cpu", help="gpu is experimental, see landmarks.py")
    ap.add_argument("--arduino", default="auto", help="glove serial port, e.g. /dev/cu.usbmodem1101 or COM5. auto = find it, off = no glove")
    ap.add_argument("--no-window", action="store_true", help="no tuning window. Use this on the demo machine")
    ap.add_argument("--windowed", action="store_true", help="tuning window not fullscreen (W toggles)")
    ap.add_argument("--frames", type=int, default=0, help="stop after this many frames (0 = run until Q)")
    ap.add_argument("--record", nargs="?", const="auto", help="write landmarks to a .jsonl file")
    ap.add_argument("--replay", help="run the pipeline over a recorded .jsonl instead of the camera")
    ap.add_argument("--realtime", action="store_true", help="replay at recorded speed")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE", help="override a config.py value")
    args = ap.parse_args()

    cfg = Config()
    cfg.apply_overrides(args.set)
    # The camera is opened here, on the main thread: macOS can only show its permission prompt from it.
    camera = None if args.replay else Camera(args.camera, args.width, args.height, args.fps)
    tracker = Tracker(args, cfg, camera)
    tracker.start()

    try:
        if args.no_window:
            while tracker.is_alive():
                tracker.join(timeout=0.2)
        else:
            view = DebugView()
            fullscreen = not args.windowed
            # Fullscreen by default: the crosshair is mapped to the whole screen, like the game's will be.
            # In a small window it would travel less than your finger points and feel wrong.
            cv2.namedWindow(WINDOW, cv2.WINDOW_NORMAL)
            if fullscreen:
                cv2.setWindowProperty(WINDOW, cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)
            while tracker.is_alive():
                got = tracker.newest()
                if got is not None:
                    bgr, model, events = got
                    low = tracker.fps < LOW_FPS and tracker.counts["frames"] > 90 and not args.replay
                    cv2.imshow(WINDOW, view.draw(bgr, model, events, tracker.fps, tracker.ms, tracker.recorder is not None, low,
                                                 tracker.glove.status if tracker.glove else "glove off"))
                key = cv2.waitKey(1 if got is not None else 5) & 0xFF
                if key in (ord("q"), 27):
                    break
                if key == ord("w"):
                    fullscreen = not fullscreen
                    cv2.setWindowProperty(WINDOW, cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN if fullscreen else cv2.WINDOW_NORMAL)
                elif key != 255 and chr(key) in "cnx[]fr":
                    tracker.keys.put(chr(key))
    except KeyboardInterrupt:
        pass
    finally:
        tracker.stop_flag.set()
        tracker.join(timeout=3.0)
        if camera:
            camera.close()
        if not args.no_window:
            cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
