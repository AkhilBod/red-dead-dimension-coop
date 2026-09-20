"""Synthesise placeholder sound effects (original, no third-party samples) into Scripts/audio_src/*.wav.
Run with any Python that has numpy. Then Scripts/import_audio.py brings them into /Game/IronHorse/audio."""
import os, wave
import numpy as np

SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "audio_src")
rng = np.random.default_rng(7)

def t(sec): return np.arange(int(SR * sec)) / SR
def noise(sec): return rng.uniform(-1, 1, int(SR * sec))
def env(sec, attack=0.002, decay=8.0):
    x = t(sec)
    return np.minimum(1, x / attack) * np.exp(-decay * x)
def lowpass(x, k):
    y = np.empty_like(x); acc = 0.0
    for i, v in enumerate(x):
        acc += k * (v - acc); y[i] = acc
    return y
def save(name, x, gain=0.9):
    x = x / (np.max(np.abs(x)) + 1e-9) * gain
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((x * 32767).astype(np.int16).tobytes())

def shot(sec, body_hz, decay, crack):
    x = t(sec)
    boom = np.sin(2 * np.pi * body_hz * x * np.exp(-6 * x)) * np.exp(-decay * x)
    return lowpass(noise(sec), 0.35) * env(sec, 0.001, decay * 1.4) * crack + boom + lowpass(noise(sec), 0.02) * np.exp(-3 * x) * 0.5

os.makedirs(OUT, exist_ok=True)
save("shot_player", shot(0.7, 140, 9, 1.2))
save("shot_enemy", shot(0.6, 90, 11, 0.7), 0.6)
x = t(0.09); save("dry", (noise(0.09) * 0.4 + np.sin(2 * np.pi * 2400 * x)) * env(0.09, 0.001, 60), 0.5)
x = t(0.5); clack = lambda at, hz: np.where(x > at, np.sin(2 * np.pi * hz * (x - at)) * np.exp(-45 * np.clip(x - at, 0, None)), 0) + np.where(x > at, noise(0.5) * np.exp(-70 * np.clip(x - at, 0, None)), 0) * 0.6
save("reload", clack(0.0, 900) + clack(0.16, 1300) + clack(0.30, 700), 0.8)
x = t(0.45); save("whiz", np.sin(2 * np.pi * (2600 * np.exp(-3.5 * x) + 400) * x) * np.sin(np.pi * x / 0.45) ** 2 + lowpass(noise(0.45), 0.2) * np.sin(np.pi * x / 0.45) ** 2 * 0.4, 0.6)
x = t(0.35); save("hurt", np.sin(2 * np.pi * 70 * x) * np.exp(-9 * x) + lowpass(noise(0.35), 0.05) * np.exp(-12 * x), 0.95)
x = t(0.3); save("grunt", lowpass(np.sign(np.sin(2 * np.pi * (150 - 120 * x) * x)) * np.exp(-9 * x), 0.12), 0.55)
x = t(2.2); save("bell", sum(a * np.sin(2 * np.pi * f * x) * np.exp(-d * x) for f, a, d in ((620, 1, 1.6), (1243, 0.6, 2.2), (1871, 0.4, 3.0), (2530, 0.25, 4.0))), 0.8)
x = t(1.8); save("whistle", (np.sin(2 * np.pi * 660 * x) + np.sin(2 * np.pi * 792 * x) + 0.6 * np.sin(2 * np.pi * 1320 * x) + lowpass(noise(1.8), 0.3) * 0.5) * np.minimum(1, x / 0.08) * np.minimum(1, (1.8 - x) / 0.35), 0.7)
x = t(0.35); save("glass", sum(np.sin(2 * np.pi * f * x) * np.exp(-d * x) for f, d in ((3100, 18), (4300, 25), (5200, 30), (6900, 40))) + noise(0.35) * np.exp(-30 * x), 0.7)
x = t(0.5); save("ricochet", np.sin(2 * np.pi * (3200 * np.exp(-5 * x) + 600) * x) * np.exp(-7 * x) + noise(0.5) * np.exp(-60 * x) * 0.5, 0.6)
x = t(0.7); save("telegraph", np.sin(2 * np.pi * (500 + 900 * (x / 0.7) ** 2) * x) * (x / 0.7) ** 1.5 * np.minimum(1, (0.7 - x) / 0.03), 0.35)
x = t(1.4); save("boom", np.sin(2 * np.pi * 55 * x * np.exp(-1.5 * x)) * np.exp(-3 * x) + lowpass(noise(1.4), 0.04) * np.exp(-3.5 * x) * 1.2 + lowpass(noise(1.4), 0.4) * np.exp(-25 * x), 1.0)
x = t(0.3); save("bonk", np.sin(2 * np.pi * 180 * x) * np.exp(-14 * x) + np.sin(2 * np.pi * 95 * x) * np.exp(-10 * x), 0.95)
x = t(0.4); save("thud", np.sin(2 * np.pi * 60 * x) * np.exp(-12 * x) + lowpass(noise(0.4), 0.03) * np.exp(-15 * x), 0.9)
x = t(3.2); beat = lambda at: np.where(x > at, np.sin(2 * np.pi * 50 * (x - at)) * np.exp(-16 * np.clip(x - at, 0, None)), 0)
save("heartbeat", sum(beat(b) + 0.7 * beat(b + 0.22) for b in (0.0, 0.8, 1.6, 2.4)), 0.9)
# Train: four chuffs a bar over a rumble, loops cleanly at 1.6 s.
L = 1.6; x = t(L); chuff = np.zeros_like(x)
for k in range(4):
    at = k * L / 4; d = np.clip(x - at, 0, None)
    chuff += np.where(x >= at, lowpass(noise(L), 0.12) * np.exp(-11 * d) * (1.0 if k % 2 == 0 else 0.7), 0)
rumble = lowpass(noise(L), 0.015) * 0.7; fade = int(SR * 0.05)
rumble[:fade] = rumble[:fade] * np.linspace(0, 1, fade) + rumble[-fade:] * np.linspace(1, 0, fade)
clack2 = sum(np.where(x >= at, np.sin(2 * np.pi * 420 * (x - at)) * np.exp(-60 * np.clip(x - at, 0, None)), 0) for at in (0.0, 0.11, 0.8, 0.91)) * 0.35
save("train_loop", chuff + rumble + clack2, 0.7)
print("wrote", len(os.listdir(OUT)), "files to", OUT)
