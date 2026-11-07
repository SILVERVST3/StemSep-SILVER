#pragma once
// STFT/iSTFT identica ao torch.stft/istft usado pelo SCNet:
//   n_fft = 4096, hop = 1024, janela retangular (window=None), center=True (reflect),
//   normalized=True (escala 1/sqrt(n_fft) na ida e na volta), onesided.

#include <complex>
#include <cstdint>
#include <vector>

namespace stemsep
{

class ModelStft
{
public:
    static constexpr int kNfft  = 4096;
    static constexpr int kHop   = 1024;
    static constexpr int kFreqs = kNfft / 2 + 1;   // 2049

    // Numero de quadros para um sinal de 'length' amostras (center=True).
    static int numFrames (int64_t length) noexcept { return (int) (length / kHop) + 1; }

    // x: sinal mono de 'length' amostras.
    // outRe/outIm: ponteiros para [kFreqs * frameStride] com layout [f * frameStride + t].
    static void forward (const float* x, int64_t length, float* outRe, float* outIm, int frameStride);

    // Inverso: re/im no mesmo layout, nFrames quadros. Saida com hop*(nFrames-1) amostras.
    static void inverse (const float* re, const float* im, int nFrames, int frameStride,
                         float* out, int64_t outLength);
};

} // namespace stemsep
