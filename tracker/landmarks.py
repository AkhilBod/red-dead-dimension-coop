"""Landmark data types, the MediaPipe wrapper, and record/replay of landmark streams."""
import json
import sys
from concurrent.futures import ThreadPoolExecutor
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
    """Runs the hand and pose models on their own threads so they overlap."""

    def __init__(self, cfg, num_hands=2, pose_every=1, delegate=None):
        import mediapipe as mp
        self.mp = mp
        # mediapipe 1.x macOS wheels abort on the CPU path ("Service is unavailable" from
        # their Metal helper), so macOS runs on the GPU delegate, which wants RGBA input.
        # Everywhere else the Python GPU delegate does not exist, so CPU + RGB.
        self.gpu = (delegate or ("gpu" if sys.platform == "darwin" else "cpu")) == "gpu"
        for name, url in MODEL_URLS.items():
            if not (MODELS_DIR / name).exists():
                raise SystemExit(f"missing {MODELS_DIR / name}\ndownload it from {url}")
        self.pose_every = max(1, pose_every)
        self._count = 0
        self._last_ts = -1
        self._last_pose = None
        self._hand_thread = ThreadPoolExecutor(1)
        self._pose_thread = ThreadPoolExecutor(1)
        vision, base = mp.tasks.vision, mp.tasks.BaseOptions
        device = base.Delegate.GPU if self.gpu else base.Delegate.CPU
        self._hands = self._hand_thread.submit(lambda: vision.HandLandmarker.create_from_options(vision.HandLandmarkerOptions(
            base_options=base(model_asset_path=str(MODELS_DIR / "hand_landmarker.task"), delegate=device),
            running_mode=vision.RunningMode.VIDEO,
            num_hands=num_hands,
            min_hand_detection_confidence=cfg.hand_detect_conf,
            min_hand_presence_confidence=cfg.hand_detect_conf,
            min_tracking_confidence=cfg.hand_track_conf,
        ))).result()
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
        hands_job = self._hand_thread.submit(self._hands.detect_for_video, image, ts)
        pose_job = None
        if self._count % self.pose_every == 0:
            pose_job = self._pose_thread.submit(self._pose.detect_for_video, image, ts)
        self._count += 1

        hr = hands_job.result()
        hands = []
        for i, lms in enumerate(hr.hand_landmarks):
            world = _xyz(hr.hand_world_landmarks[i]) if i < len(hr.hand_world_landmarks) else None
            cat = hr.handedness[i][0] if i < len(hr.handedness) and hr.handedness[i] else None
            hands.append(Hand(_xyz(lms), world, cat.category_name if cat else "", float(cat.score) if cat else 0.0))

        if pose_job is not None:
            pr = pose_job.result()
            if pr.pose_landmarks:
                lms = pr.pose_landmarks[0]
                vis = np.array([getattr(p, "visibility", None) or 0.0 for p in lms], dtype=float)
                self._last_pose = Pose(_xyz(lms), vis)
            else:
                self._last_pose = None
        return Frame(t, bgr.shape[1] / bgr.shape[0], hands, self._last_pose)

    def close(self):
        self._hand_thread.submit(self._hands.close).result()
        self._pose_thread.submit(self._pose.close).result()
        self._hand_thread.shutdown()
        self._pose_thread.shutdown()
