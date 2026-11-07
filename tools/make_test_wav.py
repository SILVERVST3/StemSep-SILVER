#!/usr/bin/env python3
"""Gera uma 'musica' sintetica curta (bateria, baixo, acordes e uma voz) para o teste de fumaca do CI."""
import sys
import wave

import numpy as np


def main(path, sr=48000, seconds=12.0):
    n = int(sr * seconds)
    t = np.arange(n) / sr
    rng = np.random.default_rng(7)
    out = np.zeros((n, 2))
    beat = 60.0 / 110.0
    # bateria: bumbo + caixa + chimbal
    for k in range(int(seconds / beat * 2)):
        start = int(k * beat / 2 * sr)
        if start >= n:
            break
        seg = np.arange(min(int(0.25 * sr), n - start)) / sr
        if k % 4 == 0:
            out[start:start + len(seg)] += (0.6 * np.sin(2 * np.pi * (50 + 60 * np.exp(-seg * 30)) * seg) * np.exp(-seg * 12))[:, None]
        if k % 4 == 2:
            out[start:start + len(seg)] += (0.3 * rng.standard_normal(len(seg)) * np.exp(-seg * 25))[:, None]
        out[start:start + len(seg)] += (0.05 * rng.standard_normal(len(seg)) * np.exp(-seg * 80))[:, None]
    # baixo
    notes = [55.0, 55.0, 73.4, 65.4]
    f_bass = np.array([notes[int(x / (2 * beat)) % 4] for x in t])
    phase = 2 * np.pi * np.cumsum(f_bass) / sr
    out += (0.25 * (np.sin(phase) + 0.3 * np.sin(2 * phase)))[:, None]
    # acordes (instrumentos)
    for f in (261.6, 329.6, 392.0):
        out[:, 0] += 0.06 * np.sin(2 * np.pi * f * t)
        out[:, 1] += 0.06 * np.sin(2 * np.pi * f * 1.002 * t)
    # "voz": vibrato + harmonicos
    f0 = 220 * (1 + 0.01 * np.sin(2 * np.pi * 5.5 * t))
    ph = 2 * np.pi * np.cumsum(f0) / sr
    voice = sum((0.12 / h) * np.sin(h * ph) for h in range(1, 8)) * (0.5 + 0.5 * np.sin(2 * np.pi * 0.25 * t) ** 2)
    out += voice[:, None]
    out *= 0.7 / np.max(np.abs(out))
    pcm = (out * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())
    print("gerado", path, sr, "Hz", seconds, "s")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "teste.wav")
