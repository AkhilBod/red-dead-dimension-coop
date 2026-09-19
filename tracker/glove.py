"""The Arduino glove: a physical trigger in, buzzer + LED feedback out.

Firmware and protocol: arduino/finger_gun_glove/finger_gun_glove.ino. Entirely optional:
with no board plugged in the tracker runs exactly as before. The link lives on its own
thread and keeps trying, so the glove can be plugged in or pulled out mid-session.
"""
import queue
import threading
import time

import serial
from serial.tools import list_ports

BAUD = 115200
FIRE, RELOAD, HIT = b"F", b"R", b"H"
LIKELY = ("arduino", "usbmodem", "usbserial", "wchusb", "ch340", "cp210", "ftdi", "usb serial", "ttyacm", "ttyusb")


def likely_ports():
    return [p.device for p in list_ports.comports()
            if any(k in f"{p.device} {p.description} {p.manufacturer}".lower() for k in LIKELY)]


class Glove:
    def __init__(self, port="auto", handshake=True):
        self.port = port
        self.handshake = handshake
        self.presses = queue.SimpleQueue()
        self.ser = None
        self.status = "no glove"
        self.running = True
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _is_our_glove(self, ser):
        """An Uno resets when the port opens and says READY about 2 s later. Anything else
        that happens to be a serial device stays silent, and we leave it alone."""
        if not self.handshake:
            return True
        deadline, buf, asked = time.monotonic() + 4.5, b"", time.monotonic() + 1.5    # let the bootloader finish before poking it
        while self.running and time.monotonic() < deadline:
            if time.monotonic() - asked > 0.5:
                ser.write(b"?")
                asked = time.monotonic()
            buf += ser.read(64)
            if any(word in buf for word in (b"READY", b"FG1")):
                return True
        return False

    def _connect(self):
        for dev in (likely_ports() if self.port == "auto" else [self.port]):
            try:
                ser = serial.serial_for_url(dev, BAUD, timeout=0.05)
                if self._is_our_glove(ser):
                    self.ser, self.status = ser, f"glove on {dev}"
                    print(f"[glove] connected on {dev}")
                    return True
                ser.close()
            except (serial.SerialException, OSError):
                pass
        return False

    def _run(self):
        buf = b""
        while self.running:
            if self.ser is None:
                if not self._connect():
                    time.sleep(2.0)
                continue
            try:
                buf += self.ser.read(64)
            except (serial.SerialException, OSError):
                print("[glove] disconnected")
                with self.lock:
                    self.ser, self.status, buf = None, "no glove", b""
                continue
            while b"\n" in buf:
                line, _, buf = buf.partition(b"\n")
                if line.strip() == b"T":
                    self.presses.put(time.monotonic())

    def poll(self):
        """Times (time.monotonic) of trigger presses since the last call."""
        out = []
        while True:
            try:
                out.append(self.presses.get_nowait())
            except queue.Empty:
                return out

    def send(self, code):
        with self.lock:
            if self.ser is not None:
                try:
                    self.ser.write(code)
                except (serial.SerialException, OSError):
                    pass

    def close(self):
        self.running = False
        self.thread.join(timeout=1.0)
        with self.lock:
            if self.ser is not None:
                self.ser.close()
                self.ser = None
