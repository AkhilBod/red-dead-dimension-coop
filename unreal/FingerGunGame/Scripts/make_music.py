"""Three original loops, synthesised (no samples, nothing to license): plucked-string guitar is Karplus-Strong,
the whistle and harmonica are shaped oscillators. Spaghetti-western by way of inspiration only.
  music_day    galloping ride, E minor, 104 bpm
  music_night  sparse night heist, D minor, 76 bpm
  music_boss   the duel: bell, heartbeat, whistle, A minor, 60 bpm
Run with a Python that has numpy + scipy, then import_audio.py."""
import os, wave
import numpy as np
from scipy.signal import lfilter

SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "audio_src")
rng = np.random.default_rng(11)
NOTE = {n: i for i, n in enumerate(["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"])}

def hz(name):
    return 440.0 * 2 ** ((NOTE[name[:-1]] + 12 * (int(name[-1]) + 1) - 69) / 12)

def pluck(freq, sec, bright=0.5, damp=0.996):
    """Karplus-Strong as one IIR filter over a noise burst."""
    n = int(SR / freq)
    burst = rng.uniform(-1, 1, n)
    burst = lfilter([1 - bright, bright], [1], burst)            # darker pick
    x = np.zeros(int(SR * sec)); x[:n] = burst
    a = np.zeros(n + 2); a[0] = 1; a[n] = -0.5 * damp; a[n + 1] = -0.5 * damp
    return lfilter([1], a, x)

def tone(freq, sec, vibrato=5.5, depth=0.012, harmonics=(1, 0.0, 0.15), attack=0.08, release=0.25):
    t = np.arange(int(SR * sec)) / SR
    phase = 2 * np.pi * freq * (t + depth / (2 * np.pi * vibrato) * np.sin(2 * np.pi * vibrato * t) * np.minimum(1, t / 0.3))
    y = sum(a * np.sin(k * phase) for k, a in enumerate(harmonics, 1))
    return y * np.minimum(1, t / attack) * np.minimum(1, (sec - t) / release)

def drum(sec, f0=70, drop=30, noise=0.0):
    t = np.arange(int(SR * sec)) / SR
    y = np.sin(2 * np.pi * (f0 * t + drop * (1 - np.exp(-t * 18)) / 18)) * np.exp(-t * 14)
    return y + noise * rng.uniform(-1, 1, len(t)) * np.exp(-t * 40)

def shaker(sec):
    t = np.arange(int(SR * sec)) / SR
    return lfilter([1, -0.95], [1], rng.uniform(-1, 1, len(t))) * np.exp(-t * 45)

class Mix:
    def __init__(self, beats, bpm):
        self.beat = 60.0 / bpm
        self.n = int(SR * beats * self.beat)
        self.buf = np.zeros(self.n)
    def add(self, at_beat, y, gain=1.0):
        i = int(at_beat * self.beat * SR) % self.n
        y = y * gain
        while len(y):                                              # tails wrap round, so the loop has no seam
            k = min(len(y), self.n - i)
            self.buf[i:i + k] += y[:k]
            y, i = y[k:], 0
    def save(self, name, gain=0.85):
        y = self.buf
        echo = np.zeros_like(y)                                    # a little canyon
        for d, g in ((0.29, 0.28), (0.47, 0.18), (0.83, 0.10)):
            echo += np.roll(y, int(d * SR)) * g
        y = np.tanh((y + echo) * 1.2)
        y = y / np.max(np.abs(y)) * gain
        with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
            w.writeframes((y * 32767).astype(np.int16).tobytes())
        print(name, f"{self.n / SR:.1f} s")

os.makedirs(OUT, exist_ok=True)

# ---- day: the ride. Gallop (da-da-DUM) on muted low strings, a twangy tune over the top, 16 bars of 4.
m = Mix(64, 104)
bass_line = ["E2", "E2", "G2", "A2", "E2", "E2", "D2", "B1"] * 2
for bar, root in enumerate(bass_line):
    b = bar * 4
    for beat in range(4):
        m.add(b + beat, pluck(hz(root), 0.5, 0.3, 0.990), 0.9)
        m.add(b + beat + 0.5, pluck(hz(root) * 2, 0.22, 0.35, 0.975), 0.45)
        m.add(b + beat + 0.75, pluck(hz(root) * 2, 0.22, 0.35, 0.975), 0.5)
        m.add(b + beat, shaker(0.12), 0.10); m.add(b + beat + 0.5, shaker(0.08), 0.07); m.add(b + beat + 0.75, shaker(0.08), 0.08)
    if bar % 2 == 1:
        for k, nt in enumerate(("E3", "G3", "B3")):
            m.add(b + 2 + k * 0.03, pluck(hz(nt), 1.6, 0.6), 0.35)
tune = [(0, "E4", 1.5), (1.5, "G4", .5), (2, "A4", 1), (3, "B4", 1), (4, "A4", 1.5), (5.5, "G4", .5), (6, "E4", 2),
        (8, "D4", 1), (9, "E4", 1), (10, "G4", 1.5), (11.5, "E4", .5), (12, "D4", 1), (13, "B3", 1), (14, "E4", 2),
        (16, "B4", 1.5), (17.5, "A4", .5), (18, "G4", 1), (19, "A4", 1), (20, "B4", 1), (21, "D5", 1), (22, "B4", 2),
        (24, "A4", 1), (25, "G4", 1), (26, "E4", 1.5), (27.5, "D4", .5), (28, "E4", 4)]
for rep in (0, 32):
    for at, nt, ln in tune:
        m.add(rep + at, pluck(hz(nt), ln * m.beat + 0.9, 0.85, 0.9975), 0.8)
        if rep: m.add(rep + at, pluck(hz(nt) * 2, ln * m.beat + 0.5, 0.9, 0.995), 0.2)
m.save("music_day")

# ---- night: the heist. Slow, low, lots of air. A pad, single tremolo notes, a harmonica answering.
m = Mix(32, 76)
for bar, (root, third, fifth) in enumerate([("D2", "F3", "A3"), ("D2", "F3", "A3"), ("A#1", "D3", "F3"), ("C2", "E3", "G3")] * 2):
    b = bar * 4
    for nt in (root, third, fifth):
        m.add(b, tone(hz(nt), 4 * m.beat + 0.6, vibrato=0.3, depth=0.004, harmonics=(1, 0.3, 0.1), attack=1.2, release=1.4), 0.10)
    m.add(b, pluck(hz(root), 3.0, 0.25, 0.9985), 0.9)
    m.add(b + 2.5, pluck(hz(root) * 1.5, 1.2, 0.3, 0.996), 0.4)
    m.add(b + 1, drum(0.5, 55, 20), 0.35); m.add(b + 3, drum(0.4, 55, 20), 0.22)
    for k in range(8):                                             # tremolo-picked high note, fading
        m.add(b + 2 + k * 0.125, pluck(hz(fifth) * 2, 0.35, 0.9, 0.992), 0.22 * (1 - k / 9))
for at, nt, ln in [(4, "A4", 2.5), (7, "F4", 1), (8, "G4", 3), (12, "D4", 3.5), (20, "A4", 1.5), (21.5, "C5", 1), (22.5, "A4", 1.5), (24, "G4", 2), (28, "D4", 3.5)]:
    m.add(at, tone(hz(nt), ln * m.beat, vibrato=5.0, depth=0.02, harmonics=(1, 0.5, 0.35, 0.2), attack=0.12, release=0.4), 0.22)
m.save("music_night")

# ---- boss: the duel. A bell on the one, a heartbeat, a lone whistle, a strummed answer. 16 bars of 4 at 60.
m = Mix(64, 60)
def bell(sec=4.0):
    t = np.arange(int(SR * sec)) / SR
    return sum(a * np.sin(2 * np.pi * f * t) * np.exp(-d * t) for f, a, d in ((220, 1, 0.9), (441, 0.6, 1.3), (663, 0.35, 1.9), (905, 0.2, 2.6)))
chords = [("A2", "A3", "C4", "E4"), ("A2", "A3", "C4", "E4"), ("F2", "A3", "C4", "F4"), ("E2", "G#3", "B3", "E4")] * 4
for bar, ch in enumerate(chords):
    b = bar * 4
    if bar % 2 == 0: m.add(b, bell(), 0.35)
    for beat in (0, 2):
        m.add(b + beat, drum(0.45, 60, 25), 0.75); m.add(b + beat + 0.3, drum(0.4, 55, 20), 0.5)
    m.add(b, pluck(hz(ch[0]), 3.5, 0.3, 0.9985), 0.9)
    if bar >= 4:
        for k, nt in enumerate(ch[1:]):
            m.add(b + 3 + k * 0.04, pluck(hz(nt), 1.4, 0.7), 0.4)
    if bar >= 8:
        for k in range(6):
            m.add(b + 1 + k / 6, pluck(hz(ch[3]) * 2, 0.3, 0.9, 0.99), 0.16)
whistle = [(8, "A5", 1.5), (9.5, "E5", .5), (10, "A5", 1.5), (11.5, "E5", .5), (12, "A5", 3), (16, "C6", 1), (17, "B5", 1), (18, "A5", 1), (19, "G#5", 1), (20, "A5", 4),
           (40, "A5", 1.5), (41.5, "E5", .5), (42, "A5", 1.5), (43.5, "E5", .5), (44, "C6", 3), (48, "D6", 1), (49, "C6", 1), (50, "B5", 1), (51, "G#5", 1), (52, "A5", 4)]
for at, nt, ln in whistle:
    m.add(at, tone(hz(nt), ln * m.beat, vibrato=6.0, depth=0.015, harmonics=(1, 0.04), attack=0.1, release=0.3), 0.30)
m.save("music_boss")
