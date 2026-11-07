#!/usr/bin/env python3
"""
Separacao de referencia em Python (ONNX Runtime) - replica o MSST demix() com o SCNet.
Serve para validar o motor C++ do plugin (os dois devem dar o mesmo resultado).

  python ref_separate.py --onnx StemSep_SCNet_XL.onnx --input mix.wav --outdir out/ [--overlap 4]
"""
import argparse
import os

import numpy as np
import onnxruntime as ort
import soundfile as sf
import torch

from export_scnet_onnx import CHUNK, scnet_padding, torch_stft, torch_istft

SOURCES = ["drums", "bass", "other", "vocals"]


def windowing_array(size, fade):
    w = np.ones(size, dtype=np.float32)
    w[:fade] = np.linspace(0, 1, fade, dtype=np.float32)
    w[-fade:] = np.linspace(1, 0, fade, dtype=np.float32)
    return w


def run_chunk(sess, chunk):
    padding, _ = scnet_padding(CHUNK)
    x = torch.from_numpy(chunk[None]).float()
    x = torch.nn.functional.pad(x, (0, padding))
    spec = torch_stft(x)
    y = sess.run(["stems"], {"spec": spec.numpy()})[0]
    return torch_istft(torch.from_numpy(y), 4, CHUNK)[0].numpy()   # [4, 2, CHUNK]


def demix(sess, mix, overlap=4, progress=True):
    """mix: [2, N] float32 @ 44.1 kHz -> [4, 2, N]"""
    chunk = CHUNK
    step = chunk // overlap
    fade = chunk // 10
    border = chunk - step
    n0 = mix.shape[-1]
    padded = False
    if n0 > 2 * border and border > 0:
        mix = np.pad(mix, ((0, 0), (border, border)), mode="reflect")
        padded = True
    n = mix.shape[-1]
    result = np.zeros((4, 2, n), dtype=np.float32)
    counter = np.zeros(n, dtype=np.float32)
    base_win = windowing_array(chunk, fade)
    i = 0
    count = 0
    total = (n + step - 1) // step
    while i < n:
        part = mix[:, i:i + chunk]
        clen = part.shape[-1]
        mode = "reflect" if clen > chunk // 2 else "constant"
        if clen < chunk:
            part = np.pad(part, ((0, 0), (0, chunk - clen)), mode=mode)
        out = run_chunk(sess, part.astype(np.float32))
        w = base_win.copy()
        if i == 0:
            w[:fade] = 1
        if i + step >= n:
            w[-fade:] = 1
        result[..., i:i + clen] += out[..., :clen] * w[:clen]
        counter[i:i + clen] += w[:clen]
        i += step
        count += 1
        if progress:
            print(f"  chunk {count}/{total}", flush=True)
    est = result / counter
    if padded:
        est = est[..., border:-border]
    return est


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx", required=True)
    ap.add_argument("--input", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--overlap", type=int, default=4)
    ap.add_argument("--threads", type=int, default=0)
    args = ap.parse_args()
    so = ort.SessionOptions()
    if args.threads:
        so.intra_op_num_threads = args.threads
    sess = ort.InferenceSession(args.onnx, so, providers=["CPUExecutionProvider"])
    x, sr = sf.read(args.input, dtype="float32", always_2d=True)
    assert sr == 44100, "referencia so para 44.1 kHz"
    mix = x.T.copy()
    if mix.shape[0] == 1:
        mix = np.repeat(mix, 2, axis=0)
    est = demix(sess, mix[:2], args.overlap)
    os.makedirs(args.outdir, exist_ok=True)
    for k, name in enumerate(SOURCES):
        sf.write(os.path.join(args.outdir, f"{name}.wav"), est[k].T, sr, subtype="FLOAT")
    print("ok")


if __name__ == "__main__":
    main()
