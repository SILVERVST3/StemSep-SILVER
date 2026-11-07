#!/usr/bin/env python3
"""
StemSep SILVER - exporta o modelo SCNet (4 stems: drums, bass, other, vocals) para ONNX.

O SCNet original faz STFT/iSTFT dentro do forward() e usa torch.fft.rfft/irfft no
eixo do tempo da rede de separacao. Essas operacoes nao exportam bem para ONNX
(e o DirectML nao as suporta de forma confiavel). Por isso:

  * a STFT/iSTFT sai do grafo (o plugin faz em C++, bit a bit igual ao torch.stft);
  * rfft/irfft do eixo do tempo viram multiplicacoes por matrizes DFT constantes
    (matematicamente identicas, so MatMul no grafo).

Entrada do grafo  : "spec"  float32 [B, 4, 2049, 476]   (L/R x real/imag, STFT normalizada)
Saida do grafo    : "stems" float32 [B, 16, 2049, 476]  (canal = 4*fonte + 2*canal_audio + re/im)

Uso:
  python export_scnet_onnx.py --ckpt model_scnet_ep_36_sdr_10.0891.ckpt \
      --config config_musdb18_scnet_xl_more_wide_v5.yaml --out StemSep_SCNet_XL.onnx [--verify]
"""
import argparse
import math
import os
import sys

import numpy as np
import torch
import torch.nn as nn
import yaml

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scnet import SCNet  # noqa: E402

CHUNK = 485100          # 11 s @ 44.1 kHz (chunk de treino do modelo)
NFFT = 4096
HOP = 1024
FREQS = NFFT // 2 + 1   # 2049


def scnet_padding(length: int):
    """Replica o padding do SCNet.forward (garante numero par de quadros)."""
    padding = HOP - length % HOP
    if (length + padding) // HOP % 2 == 0:
        padding += HOP
    frames = (length + padding) // HOP + 1
    return padding, frames


class MatmulFeatureConversion(nn.Module):
    """rfft/irfft (norm='ortho') no ultimo eixo, implementado como MatMul."""

    def __init__(self, channels: int, inverse: bool, n_time: int):
        super().__init__()
        assert n_time % 2 == 0
        self.inverse = inverse
        self.channels = channels
        n = n_time
        k = n // 2 + 1
        nn_ = torch.arange(n, dtype=torch.float64)
        kk = torch.arange(k, dtype=torch.float64)
        ang = 2.0 * math.pi * torch.outer(nn_, kk) / n          # [N, K]
        scale = 1.0 / math.sqrt(n)
        if not inverse:
            self.register_buffer("mat_re", (torch.cos(ang) * scale).float())    # [N, K]
            self.register_buffer("mat_im", (-torch.sin(ang) * scale).float())   # [N, K]
        else:
            w = torch.full((k,), 2.0, dtype=torch.float64)
            w[0] = 1.0
            w[-1] = 1.0
            self.register_buffer("mat_re", (w[:, None] * torch.cos(ang.T) * scale).float())   # [K, N]
            self.register_buffer("mat_im", (-w[:, None] * torch.sin(ang.T) * scale).float())  # [K, N]

    def forward(self, x):
        if self.inverse:
            half = self.channels // 2
            xr = x[:, :half]
            xi = x[:, half:]
            return torch.matmul(xr, self.mat_re) + torch.matmul(xi, self.mat_im)
        re = torch.matmul(x, self.mat_re)
        im = torch.matmul(x, self.mat_im)
        return torch.cat([re, im], dim=1)


class SCNetCore(nn.Module):
    """Parte do SCNet entre a STFT e a iSTFT (exportavel para ONNX)."""

    def __init__(self, model: SCNet, n_frames: int):
        super().__init__()
        self.encoder = model.encoder
        self.decoder = model.decoder
        self.separation_net = model.separation_net
        convs = []
        for fc in model.separation_net.feature_conversion:
            convs.append(MatmulFeatureConversion(fc.channels, fc.inverse, n_frames))
        self.separation_net.feature_conversion = nn.ModuleList(convs)

    def forward(self, spec):
        x = spec
        skips, lens, olens = [], [], []
        for sd_layer in self.encoder:
            x, skip, lengths, original_lengths = sd_layer(x)
            skips.append(skip)
            lens.append(lengths)
            olens.append(original_lengths)
        x = self.separation_net(x)
        for fusion_layer, su_layer in self.decoder:
            x = fusion_layer(x, skips.pop())
            x = su_layer(x, lens.pop(), olens.pop())
        return x


def build_model(cfg_path: str, ckpt_path: str) -> SCNet:
    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = yaml.load(f, Loader=yaml.FullLoader)
    model = SCNet(**cfg["model"])
    state = torch.load(ckpt_path, map_location="cpu", weights_only=False)
    if isinstance(state, dict) and "state_dict" in state:
        state = state["state_dict"]
    model.load_state_dict(state)
    model.eval()
    return model


def torch_stft(x):
    """x [B, 2, L] -> spec [B, 4, 2049, T] (identico ao SCNet.forward)."""
    b, c, l = x.shape
    z = torch.stft(x.reshape(-1, l), n_fft=NFFT, hop_length=HOP, win_length=NFFT,
                   window=torch.ones(NFFT), center=True, normalized=True, return_complex=True)
    z = torch.view_as_real(z)                               # [B*C, F, T, 2]
    z = z.permute(0, 3, 1, 2).reshape(b, c * 2, z.shape[1], z.shape[2])
    return z


def torch_istft(y, n_sources: int, length: int):
    """y [B, 16, F, T] -> wav [B, S, 2, length]."""
    b, ch, fr, t = y.shape
    y = y.reshape(-1, 2, fr, t).permute(0, 2, 3, 1)
    y = torch.view_as_complex(y.contiguous())
    w = torch.istft(y, n_fft=NFFT, hop_length=HOP, win_length=NFFT,
                    window=torch.ones(NFFT), center=True, normalized=True)
    return w.reshape(b, n_sources, 2, -1)[..., :length]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", required=True)
    ap.add_argument("--config", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--opset", type=int, default=17)
    ap.add_argument("--verify", action="store_true", help="compara com o SCNet original e com o ONNX Runtime")
    args = ap.parse_args()

    torch.set_grad_enabled(False)
    model = build_model(args.config, args.ckpt)
    padding, n_frames = scnet_padding(CHUNK)
    print(f"chunk={CHUNK} padding={padding} frames={n_frames}")

    core = SCNetCore(build_model(args.config, args.ckpt), n_frames).eval()
    dummy = torch.randn(1, 4, FREQS, n_frames) * 0.05

    torch.onnx.export(
        core, (dummy,), args.out,
        input_names=["spec"], output_names=["stems"],
        dynamic_axes={"spec": {0: "batch"}, "stems": {0: "batch"}},
        opset_version=args.opset, do_constant_folding=True, dynamo=False,
    )
    # ONNX Runtime 1.22 (usado no plugin) le ate IR 10; fixa para garantir compatibilidade.
    import onnx
    m = onnx.load(args.out)
    if m.ir_version > 9:
        m.ir_version = 9
    onnx.save(m, args.out)
    print("ONNX salvo em", args.out, f"({os.path.getsize(args.out) / 1e6:.1f} MB)")

    if not args.verify:
        return

    # 1) SCNet original vs (STFT torch + core + iSTFT torch)
    g = torch.Generator().manual_seed(1234)
    t = torch.arange(CHUNK) / 44100.0
    mix = 0.1 * torch.randn(1, 2, CHUNK, generator=g)
    mix += 0.3 * torch.sin(2 * math.pi * 110 * t)[None, None, :]
    ref = model(mix)                                         # [1, 4, 2, L]
    xpad = torch.nn.functional.pad(mix, (0, padding))
    spec = torch_stft(xpad)
    out = core(spec)
    mine = torch_istft(out, 4, CHUNK)
    err = (ref - mine).abs().max().item()
    print(f"[verify] SCNet original vs core+STFT externa: max|dif| = {err:.3e} (pico ref {ref.abs().max().item():.3f})")

    # 2) core torch vs ONNX Runtime
    import onnxruntime as ort
    sess = ort.InferenceSession(args.out, providers=["CPUExecutionProvider"])
    onx = sess.run(["stems"], {"spec": spec.numpy()})[0]
    err2 = np.abs(onx - out.numpy()).max()
    peak = float(np.abs(out.numpy()).max())
    print(f"[verify] core torch vs ONNX Runtime: max|dif| = {err2:.3e} (pico {peak:.3f})")
    if err > 1e-4 or err2 > 2e-3 * peak:
        print("[verify] FALHOU: o modelo exportado nao confere com o original")
        sys.exit(1)
    print("[verify] OK")


if __name__ == "__main__":
    main()
