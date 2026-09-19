import queue
import threading

from pythonosc.dispatcher import Dispatcher
from pythonosc.osc_message_builder import OscMessageBuilder
from pythonosc.osc_server import ThreadingOSCUDPServer
from pythonosc.udp_client import UDPClient

import protocol as P


class OscOut:
    def __init__(self, host, port):
        self.host, self.port = host, port
        self.client = UDPClient(host, port)

    def _send(self, address, values):
        msg = OscMessageBuilder(address=address)
        for v in values:
            msg.add_arg(float(v), "f")
        try:
            self.client.send(msg.build())
        except OSError:
            # Game not running yet, or network hiccup. UDP is fire and forget.
            pass

    def state(self, state):
        self._send(P.ADDR_STATE, state.to_floats())

    def fire(self, x, y):
        self._send(P.ADDR_FIRE, [x, y])

    def reload(self):
        self._send(P.ADDR_RELOAD, [1.0])


class OscIn:
    """Commands from the game (calibration, recenter). Poll from the main loop."""

    def __init__(self, port):
        self.commands = queue.SimpleQueue()
        dispatcher = Dispatcher()
        for addr in (P.ADDR_CALIB_BEGIN, P.ADDR_CALIB_TARGET, P.ADDR_RECENTER):
            dispatcher.map(addr, self._on_message)
        self.server = None
        try:
            self.server = ThreadingOSCUDPServer(("0.0.0.0", port), dispatcher)
        except OSError as e:
            print(f"[osc] could not listen on port {port} ({e}). Game-driven calibration is off.")
            return
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def _on_message(self, address, *args):
        self.commands.put((address, tuple(float(a) for a in args if isinstance(a, (int, float)))))

    def poll(self):
        out = []
        while True:
            try:
                out.append(self.commands.get_nowait())
            except queue.Empty:
                return out

    def close(self):
        if self.server:
            # shutdown() blocks until the server loop notices. Nothing on exit needs to wait for it.
            threading.Thread(target=self.server.shutdown, daemon=True).start()
