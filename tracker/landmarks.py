"""Landmark data types, the MediaPipe wrapper, and record/replay of landmark streams."""
import json
from concurrent.futures import ThreadPoolExecutor, TimeoutError as StillRunning
from dataclasses import dataclass
from pathlib import Path

import cv2
import numpy as np

MODELS_DIR = Path(__file__).parent / "models"
MODEL_URLS = {
    "hand_landmarker.task": "https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task",
    "pose_landmarker_lite.task": "https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task",
}


@dataclass
class Hand:
    pts: np.ndarray            # (21, 3) image coords, x and y in 0..1, z relative depth
    world: np.ndarray | None   # (21, 3) metres, hand-centred. Survives foreshortening better than pts
    label: str = ""
    score: float = 0.0


@dataclass
class Pose:
    pts: np.ndarray            # (33, 3) image coords
    vis: np.ndarray            # (33,) visibility 0..1


@dataclass
class Frame:
    t: float                   # seconds, monotonic
    aspect: float              # image width / height
    hands: list
    pose: Pose | None


def frame_to_json(frame):
    def arr(a):
        return None if a is None else np.round(a, 5).tolist()
    return json.dumps({
        "t": round(frame.t, 4),
        "aspect": round(frame.aspect, 5),
        "hands": [{"pts": arr(h.pts), "world": arr(h.world), "label": h.label, "score": round(h.score, 3)} for h in frame.hands],
        "pose": None if frame.pose is None else {"pts": arr(frame.pose.pts), "vis": arr(frame.pose.vis)},
    })


def frame_from_json(line):
    d = json.loads(line)
    hands = [Hand(np.array(h["pts"]), None if h["world"] is None else np.array(h["world"]), h["label"], h["score"]) for h in d["hands"]]
    pose = None if d["pose"] is None else Pose(np.array(d["pose"]["pts"]), np.array(d["pose"]["vis"]))
    return Frame(d["t"], d["aspect"], hands, pose)


def _xyz(landmarks):
    return np.array([[p.x, p.y, p.z] for p in landmarks], dtype=float)


class Landmarker:
    """Runs the hand and pose models on their own threads so they overlap.

    Two hand models, not one. Asked for 2 hands while only 1 is in view, MediaPipe re-runs
    its expensive palm SEARCH on every frame looking for the other (measured: 48 ms a frame
    instead of 26). But the second hand only matters for a reload or an open palm. So the
    1-hand model runs normally, and the 2-hand model takes over when the second hand is
    likely to matter: the body model sees the other wrist come near the gun hand (a reload
    slap) or sees both wrists up (an open palm). It also takes a look every few frames
    regardless, and stays on while it really sees two hands: tracking two known hands is
    cheap, it is the searching that costs.
    """

    def __init__(self, cfg, num_hands=2, pose_every=1, delegate=None):
        import mediapipe as mp
        self.mp = mp
        # CPU everywhere. requirements.txt pins mediapipe 0.10.21 on purpose: on macOS, 1.0.1
        # aborts at load on the CPU path, and its GPU path leaks ~10 MB per frame until the
        # process dies a couple of minutes in. The GPU delegate wants RGBA input.
        self.gpu = delegate == "gpu"
        for name, url in MODEL_URLS.items():
            if not (MODELS_DIR / name).exists():
                raise SystemExit(f"missing {MODELS_DIR / name}\ndownload it from {url}")
        self.pose_every = max(1, pose_every)
        self._count = 0
        self._last_ts = -1
        self._last_pose = None
        self._pose_job = None
        self._pose_wait_s = cfg.pose_wait_s
        self._hand_thread = ThreadPoolExecutor(1)
        self._pose_thread = ThreadPoolExecutor(1)
        vision, base = mp.tasks.vision, mp.tasks.BaseOptions
        device = base.Delegate.GPU if self.gpu else base.Delegate.CPU
        def hand_model(n):
            return self._hand_thread.submit(lambda: vision.HandLandmarker.create_from_options(vision.HandLandmarkerOptions(
                base_options=base(model_asset_path=str(MODELS_DIR / "hand_landmarker.task"), delegate=device),
                running_mode=vision.RunningMode.VIDEO,
                num_hands=n,
                min_hand_detection_confidence=cfg.hand_detect_conf,
                min_hand_presence_confidence=cfg.hand_detect_conf,
                min_tracking_confidence=cfg.hand_track_conf,
            ))).result()
        self._one_hand = hand_model(1)
        self._two_hands = hand_model(2) if num_hands >= 2 else None
        self._scan_every = cfg.second_hand_scan_every
        self._since_scan = 0
        self._seeing_two = False
        self._misses = 0
        self._cue_off_until = -1
        self._pose = self._pose_thread.submit(lambda: vision.PoseLandmarker.create_from_options(vision.PoseLandmarkerOptions(
            base_options=base(model_asset_path=str(MODELS_DIR / "pose_landmarker_lite.task"), delegate=device),
            running_mode=vision.RunningMode.VIDEO,
            num_poses=1,
            min_pose_detection_confidence=cfg.pose_detect_conf,
            min_pose_presence_confidence=cfg.pose_detect_conf,
            min_tracking_confidence=cfg.pose_track_conf,
        ))).result()

    def process(self, bgr, t):
        """bgr: OpenCV frame, already mirrored."""
        mp = self.mp
        if self.gpu:
            image = mp.Image(image_format=mp.ImageFormat.SRGBA, data=cv2.cvtColor(bgr, cv2.COLOR_BGR2RGBA))
        else:
            image = mp.Image(image_format=mp.ImageFormat.SRGB, data=cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB))
        ts = max(int(t * 1000), self._last_ts + 1)
        self._last_ts = ts
        cue = self._other_hand_likely(bgr.shape[1] / bgr.shape[0])
        look_for_two = self._two_hands is not None and (self._seeing_two or cue or self._since_scan >= self._scan_every)
        hands_job = self._hand_thread.submit((self._two_hands if look_for_two else self._one_hand).detect_for_video, image, ts)
        # The body model never holds a frame up. If the last body job is still running (busy
        # machine), skip starting another and carry on with the newest body we have: a slightly
        # stale chest position costs little, a halved frame rate costs everything.
        if self._pose_job is not None and self._pose_job.done():
            self._take_pose()
        if self._pose_job is None and self._count % self.pose_every == 0:
            self._pose_job = self._pose_thread.submit(self._pose.detect_for_video, image, ts)
        self._count += 1

        hr = hands_job.result()
        if look_for_two:
            self._since_scan = 0
            if len(hr.hand_landmarks) >= 2:
                self._seeing_two, self._misses = True, 0
            else:
                self._misses += 1
                if self._misses >= 3:
                    self._seeing_two = False
                if cue and self._misses >= 20:
                    self._cue_off_until = ts + 2000     # body model says "two hands", hand model cannot find one: stop paying for the search
        else:
            self._since_scan += 1
        hands = []
        for i, lms in enumerate(hr.hand_landmarks):
            world = _xyz(hr.hand_world_landmarks[i]) if i < len(hr.hand_world_landmarks) else None
            cat = hr.handedness[i][0] if i < len(hr.handedness) and hr.handedness[i] else None
            hands.append(Hand(_xyz(lms), world, cat.category_name if cat else "", float(cat.score) if cat else 0.0))

        if self._pose_job is not None:
            try:
                self._pose_job.result(timeout=self._pose_wait_s)    # usually done already: same frame as the hands
                self._take_pose()
            except StillRunning:
                pass
        return Frame(t, bgr.shape[1] / bgr.shape[0], hands, self._last_pose)

    def _other_hand_likely(self, aspect):
        """From the last body result: is the other hand somewhere it would matter?"""
        pose = self._last_pose
        if pose is None or self._last_ts < self._cue_off_until or min(pose.vis[15], pose.vis[16], pose.vis[11], pose.vis[12]) < 0.5:
            return False
        p = pose.pts[:, :2] * np.array([aspect, 1.0])
        sw = np.linalg.norm(p[11] - p[12])
        together = np.linalg.norm(p[15] - p[16]) < 1.0 * sw                    # [rec] one-handed shooting keeps them 1.1-1.7 apart
        both_up = max(p[15][1], p[16][1]) < min(p[11][1], p[12][1]) + 0.45 * sw
        return bool(together or both_up)

    def _take_pose(self):
        pr, self._pose_job = self._pose_job.result(), None
        if pr.pose_landmarks:
            lms = pr.pose_landmarks[0]
            vis = np.array([getattr(p, "visibility", None) or 0.0 for p in lms], dtype=float)
            self._last_pose = Pose(_xyz(lms), vis)
        else:
            self._last_pose = None

    def close(self):
        if self._pose_job is not None:
            self._pose_job.result()
        self._hand_thread.submit(self._one_hand.close).result()
        if self._two_hands is not None:
            self._hand_thread.submit(self._two_hands.close).result()
        self._pose_thread.submit(self._pose.close).result()
        self._hand_thread.shutdown()
        self._pose_thread.shutdown()
