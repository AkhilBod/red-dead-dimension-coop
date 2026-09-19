import os
import sys
import threading
import time
import unittest

import synth  # noqa: F401  (puts the tracker folder on sys.path)

from glove import FIRE, RELOAD, Glove


class GloveLinkTests(unittest.TestCase):
    """pyserial's loop:// port echoes whatever is written, so it can play the Arduino."""

    def setUp(self):
        self.glove = Glove("loop://", handshake=False)
        deadline = time.monotonic() + 2.0
        while self.glove.ser is None and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertIsNotNone(self.glove.ser)

    def tearDown(self):
        self.glove.close()

    def _presses_within(self, seconds):
        deadline, out = time.monotonic() + seconds, []
        while time.monotonic() < deadline:
            out += self.glove.poll()
            time.sleep(0.01)
        return out

    def test_trigger_lines_become_timestamped_presses(self):
        before = time.monotonic()
        self.glove.ser.write(b"READY\r\nT\r\nFG1\r\nT\r\n")    # Serial.println ends lines with CR LF
        presses = self._presses_within(0.5)
        self.assertEqual(len(presses), 2)
        self.assertTrue(all(before <= t <= time.monotonic() for t in presses))

    def test_a_press_split_across_reads_is_still_one_press(self):
        self.glove.ser.write(b"T")
        time.sleep(0.12)
        self.glove.ser.write(b"\r\n")
        self.assertEqual(len(self._presses_within(0.5)), 1)

    def test_feedback_codes_are_not_mistaken_for_presses(self):
        self.glove.send(FIRE)
        self.glove.send(RELOAD)
        self.assertEqual(self._presses_within(0.4), [])       # the loopback echoes F and R straight back at us

    def test_sending_with_no_glove_is_harmless(self):
        unplugged = Glove("/dev/this-port-does-not-exist")
        unplugged.send(FIRE)
        self.assertEqual(unplugged.poll(), [])
        self.assertEqual(unplugged.status, "no glove")
        unplugged.close()


@unittest.skipIf(sys.platform == "win32", "needs a pseudo-terminal")
class FakeArduinoTests(unittest.TestCase):
    """A thread on the other end of a pseudo-terminal that behaves like finger_gun_glove.ino:
    silent while it "boots", then READY, answers ?, reports a press, and notes what it is told."""

    def test_handshake_press_and_feedback_round_trip(self):
        import pty
        import select
        master, slave = pty.openpty()
        received, stop = [], threading.Event()

        def arduino():
            time.sleep(0.8)                                  # an Uno resets when the port opens
            os.write(master, b"READY\r\n")
            time.sleep(0.3)
            os.write(master, b"T\r\n")
            while not stop.is_set():
                if not select.select([master], [], [], 0.05)[0]:
                    continue
                for byte in os.read(master, 64):
                    if bytes([byte]) == b"?":
                        os.write(master, b"FG1\r\n")
                    else:
                        received.append(bytes([byte]))

        board = threading.Thread(target=arduino, daemon=True)
        board.start()
        glove = Glove(os.ttyname(slave))
        try:
            deadline, presses = time.monotonic() + 6.0, []
            while not presses and time.monotonic() < deadline:
                presses += glove.poll()
                time.sleep(0.02)
            self.assertEqual(len(presses), 1)
            self.assertTrue(glove.status.startswith("glove on"))
            glove.send(FIRE)
            glove.send(RELOAD)
            deadline = time.monotonic() + 2.0
            while received[-2:] != [b"F", b"R"] and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertEqual(received[-2:], [b"F", b"R"])
        finally:
            stop.set()
            board.join(timeout=1.0)
            glove.close()
            os.close(master)
            os.close(slave)

if __name__ == "__main__":
    unittest.main()
