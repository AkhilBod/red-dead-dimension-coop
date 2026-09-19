"""Pretend to be Unreal: listen on the game port and print what the tracker sends.

    python osc_monitor.py
    python osc_monitor.py --calib    # also send a calibration sequence back to the tracker
"""
import argparse
import threading
import time

from pythonosc.dispatcher import Dispatcher
from pythonosc.osc_server import BlockingOSCUDPServer

import protocol as P
from osc_io import OscOut

CORNERS = [(0.15, 0.2), (0.85, 0.2), (0.85, 0.8), (0.15, 0.8)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=P.PORT_TO_GAME)
    ap.add_argument("--seconds", type=float, default=0, help="stop after this long (0 = forever)")
    ap.add_argument("--calib", action="store_true", help="drive a 4-corner calibration like the game would")
    ap.add_argument("--tracker-host", default="127.0.0.1")
    ap.add_argument("--tracker-port", type=int, default=P.PORT_TO_TRACKER)
    args = ap.parse_args()

    start = time.monotonic()
    stats = {"count": 0, "last_print": start, "latest": None, "bad_types": 0}
    to_tracker = OscOut(args.tracker_host, args.tracker_port)
    calib = {"index": -1}

    def next_target():
        calib["index"] += 1
        if calib["index"] < len(CORNERS):
            sx, sy = CORNERS[calib["index"]]
            to_tracker._send(P.ADDR_CALIB_TARGET, [sx, sy])
            print(f"[monitor] calib target {calib['index'] + 1}/4 at ({sx}, {sy}). Point there and fire.")
        else:
            print("[monitor] calibration sequence done")

    def on_state(address, *values):
        stats["count"] += 1
        stats["latest"] = values
        if any(not isinstance(v, float) for v in values) or len(values) != len(P.STATE_FIELDS):
            stats["bad_types"] += 1
        now = time.monotonic()
        if now - stats["last_print"] >= 1.0:
            rate = stats["count"] / (now - stats["last_print"])
            pairs = "  ".join(f"{n}={v:+.2f}" for n, v in zip(P.STATE_FIELDS, values))
            print(f"[state {rate:4.1f}Hz] {pairs}")
            if stats["bad_types"]:
                print(f"[monitor] WARNING {stats['bad_types']} state messages had wrong arg count or non-float args")
            stats.update(count=0, last_print=now, bad_types=0)

    def on_fire(address, *values):
        print(f"[FIRE  ] aim=({values[0]:.3f}, {values[1]:.3f})")
        if args.calib and 0 <= calib["index"] < len(CORNERS):
            next_target()

    def on_reload(address, *values):
        print("[RELOAD]")

    dispatcher = Dispatcher()
    dispatcher.map(P.ADDR_STATE, on_state)
    dispatcher.map(P.ADDR_FIRE, on_fire)
    dispatcher.map(P.ADDR_RELOAD, on_reload)
    server = BlockingOSCUDPServer(("0.0.0.0", args.port), dispatcher)
    print(f"[monitor] listening on udp {args.port}")

    if args.calib:
        to_tracker._send(P.ADDR_CALIB_BEGIN, [])
        next_target()
    if args.seconds > 0:
        threading.Timer(args.seconds, server.shutdown).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    server.server_close()


if __name__ == "__main__":
    main()
