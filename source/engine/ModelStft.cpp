#include "ModelStft.h"

#include <algorithm>
#include <cmath>

#include "pocketfft_hdronly.h"

namespace stemsep
{

namespace
{
    constexpr float kNorm = 1.0f / 64.0f;   // 1/sqrt(4096)

    inline float reflectAt (const float* x, int64_t length, int64_t j) noexcept
    {
        // torch/numpy 'reflect' (sem repetir a borda)
        if (j < 0)
            j = -j;
        if (j >= length)
            j = 2 * (length - 1) - j;
        if (j < 0) j = 0;                       // seguranca para sinais muito curtos
        if (j >= length) j = length - 1;
        return x[j];
    }
}

void ModelStft::forward (const float* x, int64_t length, float* outRe, float* outIm, int frameStride)
{
    const int pad = kNfft / 2;
    const int nFrames = numFrames (length);

    std::vector<float> frame ((size_t) kNfft);
    std::vector<std::complex<float>> spec ((size_t) kFreqs);

    const pocketfft::shape_t shape { (size_t) kNfft };
    const pocketfft::stride_t sIn { (ptrdiff_t) sizeof (float) };
    const pocketfft::stride_t sOut { (ptrdiff_t) sizeof (std::complex<float>) };

    for (int t = 0; t < nFrames; ++t)
    {
        const int64_t start = (int64_t) t * kHop - pad;

        if (start >= 0 && start + kNfft <= length)
        {
            std::copy (x + start, x + start + kNfft, frame.begin());
        }
        else
        {
            for (int n = 0; n < kNfft; ++n)
                frame[(size_t) n] = reflectAt (x, length, start + n);
        }

        pocketfft::r2c (shape, sIn, sOut, 0, pocketfft::FORWARD, frame.data(), spec.data(), kNorm);

        for (int f = 0; f < kFreqs; ++f)
        {
            outRe[(size_t) f * (size_t) frameStride + (size_t) t] = spec[(size_t) f].real();
            outIm[(size_t) f * (size_t) frameStride + (size_t) t] = spec[(size_t) f].imag();
        }
    }
}

void ModelStft::inverse (const float* re, const float* im, int nFrames, int frameStride,
                         float* out, int64_t outLength)
{
    const int pad = kNfft / 2;
    const int64_t fullLen = (int64_t) kNfft + (int64_t) kHop * (nFrames - 1);

    std::vector<double> ola ((size_t) fullLen, 0.0);
    std::vector<float> frame ((size_t) kNfft);
    std::vector<std::complex<float>> spec ((size_t) kFreqs);

    const pocketfft::shape_t shape { (size_t) kNfft };
    const pocketfft::stride_t sIn { (ptrdiff_t) sizeof (std::complex<float>) };
    const pocketfft::stride_t sOut { (ptrdiff_t) sizeof (float) };

    for (int t = 0; t < nFrames; ++t)
    {
        for (int f = 0; f < kFreqs; ++f)
            spec[(size_t) f] = { re[(size_t) f * (size_t) frameStride + (size_t) t],
                                 im[(size_t) f * (size_t) frameStride + (size_t) t] };

        pocketfft::c2r (shape, sIn, sOut, 0, pocketfft::BACKWARD, spec.data(), frame.data(), kNorm);

        double* dst = ola.data() + (size_t) t * kHop;
        for (int n = 0; n < kNfft; ++n)
            dst[n] += (double) frame[(size_t) n];
    }

    // envelope da janela retangular = numero de quadros que cobrem cada amostra
    const int64_t usable = std::min<int64_t> (outLength, (int64_t) kHop * (nFrames - 1));
    for (int64_t k = 0; k < usable; ++k)
    {
        const int64_t i = k + pad;
        int64_t tMax = i / kHop;
        if (tMax > nFrames - 1) tMax = nFrames - 1;
        int64_t tMin = (i - kNfft + 1 + kHop - 1) / kHop;   // ceil((i-n+1)/hop) para i-n+1 >= 0
        if (i - kNfft + 1 < 0) tMin = 0;
        const int64_t count = tMax - tMin + 1;
        out[k] = count > 0 ? (float) (ola[(size_t) i] / (double) count) : 0.0f;
    }
    for (int64_t k = usable; k < outLength; ++k)
        out[k] = 0.0f;
}

} // namespace stemsep
