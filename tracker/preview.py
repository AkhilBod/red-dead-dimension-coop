"""The small camera picture the game shows in its corner: the webcam frame with the hand skeletons on it.

JPEG over UDP to port 7002. macOS will not send a datagram over 9216 bytes, so each frame goes as slices:
b"FGCM", frame id (u16 big-endian), slice index, slice count, then the bytes. The game glues them back together.
"""
import socket
import struct

import cv2

from debug_view import GREEN, HAND_EDGES, ORANGE, YELLOW

PORT = 7002
SLICE = 8000


class PreviewSender:
    def __init__(self, host, port=PORT, width=320, every=2, quality=55):
        self.addr = (host, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.w, self.h = width, width * 9 // 16
        self.every, self.quality = every, quality
        self.n = 0

    def send(self, bgr, frame, gun=None, fired=False):
        """bgr is the mirrored camera image, frame the landmarks found in it, gun the Hand the pipeline is aiming with."""
        self.n += 1
        if bgr is None or (self.n % self.every and not fired):
            return
        w, h = self.w, self.h
        img = cv2.resize(bgr, (w, h), interpolation=cv2.INTER_AREA)
        if frame.pose is not None:
            l, r = frame.pose.pts[11], frame.pose.pts[12]
            cv2.line(img, (int(l[0] * w), int(l[1] * h)), (int(r[0] * w), int(r[1] * h)), YELLOW, 1, cv2.LINE_AA)
        for hand in frame.hands:
            color = GREEN if hand is gun else ORANGE
            pts = [(int(p[0] * w), int(p[1] * h)) for p in hand.pts]
            for a, b in HAND_EDGES:
                cv2.line(img, pts[a], pts[b], color, 1, cv2.LINE_AA)
            cv2.circle(img, pts[8], 3, color, -1, cv2.LINE_AA)
        if fired:
            cv2.rectangle(img, (0, 0), (w - 1, h - 1), (60, 60, 255), 4)
        ok, jpg = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, self.quality])
        if not ok:
            return
        data = jpg.tobytes()
        count = (len(data) + SLICE - 1) // SLICE
        if count > 255:
            return
        frame_id = (self.n // self.every) & 0xFFFF
        try:
            for i in range(count):
                self.sock.sendto(b"FGCM" + struct.pack(">HBB", frame_id, i, count) + data[i * SLICE:(i + 1) * SLICE], self.addr)
        except OSError:
            pass        # game not running. UDP is fire and forget.
