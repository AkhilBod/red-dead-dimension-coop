# Tracker

Webcam in, OSC out. The game (Unreal) listens on UDP **7000**. The tracker listens on **7001**.
The message contract is in [PLAN.md](../PLAN.md) section 4 and in [protocol.py](protocol.py).

## Setup (once)

Python 3.10 to 3.12.

```bash
cd tracker
python -m venv .venv
```

```bash
source .venv/bin/activate        # Windows: .venv\Scripts\activate
```

```bash
pip install -r requirements.txt
```

## No camera needed: fake tracker (for the Unreal side)

```bash
python fake_tracker.py
```

Mouse = aim, click = fire, R = reload, A/D = lean, S = duck, H = holster, F = off-hand open.
It sends exactly what the real tracker sends. `--auto` runs scripted motion with no window. `--host <ip>` targets another machine.

## See what is being sent

```bash
python osc_monitor.py
```

Pretends to be Unreal and prints every message. Close Unreal first, only one program can own port 7000.
