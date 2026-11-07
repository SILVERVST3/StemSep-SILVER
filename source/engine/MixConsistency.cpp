#include "MixConsistency.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

#include "pocketfft_hdronly.h"

namespace stemsep
{

namespace
{
    constexpr int kN = 2048;          // tamanho da janela
    constexpr int kH = 512;           // hop (75% de sobreposicao)
    constexpr int kBins = kN / 2 + 1;
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kOlaGain = 1.5;  // soma de hann^2 com hop N/4 (constante)

    // stems que recebem a mascara; o kOther recebe o restante exato
    constexpr std::array<int, 3> kMasked { kDrums, kBass, kVocals };
}

void applyMixConsistency (const float* mix, int64_t length, std::array<float*, kNumStems> stems)
{
    if (length <= 0)
        return;

    std::vector<float> window ((size_t) kN);
    for (int n = 0; n < kN; ++n)
        window[(size_t) n] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * n / kN));   // Hann periodica

    const pocketfft::shape_t shape { (size_t) kN };
    const pocketfft::stride_t sReal { (ptrdiff_t) sizeof (float) };
    const pocketfft::stride_t sCplx { (ptrdiff_t) sizeof (std::complex<float>) };

    std::array<std::vector<float>, kNumStems> frames;
    std::array<std::vector<std::complex<float>>, kNumStems> specs;
    for (int i = 0; i < kNumStems; ++i)
    {
        frames[(size_t) i].assign ((size_t) kN, 0.0f);
        specs[(size_t) i].assign ((size_t) kBins, {});
    }
    std::vector<float> resFrame ((size_t) kN), outFrame ((size_t) kN);
    std::vector<std::complex<float>> resSpec ((size_t) kBins), tmpSpec ((size_t) kBins);

    // buffers de correcao (janela deslizante a partir do inicio do quadro atual)
    std::array<std::vector<double>, 3> corr;
    for (auto& c : corr)
        c.assign ((size_t) (kN + kH), 0.0);

    int64_t p = -(int64_t) (kN - kH);   // inicio do primeiro quadro (cobertura completa desde a amostra 0)
    while (p < length)
    {
        // 1) quadros janelados dos stems e do residuo
        for (int n = 0; n < kN; ++n)
        {
            const int64_t s = p + n;
            const bool inside = s >= 0 && s < length;
            double sum = 0.0;
            for (int i = 0; i < kNumStems; ++i)
            {
                const float v = inside ? stems[(size_t) i][s] : 0.0f;
                sum += (double) v;
                frames[(size_t) i][(size_t) n] = v * window[(size_t) n];
            }
            const double r = inside ? ((double) mix[s] - sum) : 0.0;
            resFrame[(size_t) n] = (float) r * window[(size_t) n];
        }

        for (int i = 0; i < kNumStems; ++i)
            pocketfft::r2c (shape, sReal, sCplx, 0, pocketfft::FORWARD, frames[(size_t) i].data(), specs[(size_t) i].data(), 1.0f);
        pocketfft::r2c (shape, sReal, sCplx, 0, pocketfft::FORWARD, resFrame.data(), resSpec.data(), 1.0f);

        // 2) mascaras de Wiener (potencia) aplicadas ao residuo
        for (size_t m = 0; m < kMasked.size(); ++m)
        {
            const int stem = kMasked[m];
            for (int k = 0; k < kBins; ++k)
            {
                double total = 0.0;
                for (int i = 0; i < kNumStems; ++i)
                    total += (double) std::norm (specs[(size_t) i][(size_t) k]) + 1e-24;
                const double mask = ((double) std::norm (specs[(size_t) stem][(size_t) k]) + 1e-24) / total;
                tmpSpec[(size_t) k] = resSpec[(size_t) k] * (float) mask;
            }
            pocketfft::c2r (shape, sCplx, sReal, 0, pocketfft::BACKWARD, tmpSpec.data(), outFrame.data(), 1.0f / (float) kN);

            auto& c = corr[m];
            for (int n = 0; n < kN; ++n)
                c[(size_t) n] += (double) (outFrame[(size_t) n] * window[(size_t) n]) / kOlaGain;
        }

        // 3) as primeiras kH amostras deste quadro estao completas: aplica e avanca
        for (int k = 0; k < kH; ++k)
        {
            const int64_t s = p + k;
            if (s < 0 || s >= length)
                continue;
            double partial = 0.0;
            for (size_t m = 0; m < kMasked.size(); ++m)
            {
                float& v = stems[(size_t) kMasked[m]][s];
                v = (float) ((double) v + corr[m][(size_t) k]);
                partial += (double) v;
            }
            stems[(size_t) kOther][s] = (float) ((double) mix[s] - partial);
        }
        for (auto& c : corr)
        {
            std::memmove (c.data(), c.data() + kH, sizeof (double) * (size_t) kN);
            std::fill (c.begin() + kN, c.end(), 0.0);
        }
        p += kH;
    }
}

NullReport measureNull (const AudioData& mix, const std::array<AudioData, kNumStems>& stems)
{
    NullReport rep;
    double resEnergy = 0.0, mixEnergy = 0.0, peak = 0.0;
    const int nc = mix.numChannels();
    const int64_t ns = mix.numSamples();
    for (int c = 0; c < nc; ++c)
    {
        for (int i = 0; i < kNumStems; ++i)
            if (stems[(size_t) i].numChannels() != nc || stems[(size_t) i].numSamples() != ns)
                return rep;
        const float* x = mix.channels[(size_t) c].data();
        for (int64_t n = 0; n < ns; ++n)
        {
            // soma em float, como um DAW de 32 bits faria (pior caso)
            float sum = 0.0f;
            for (int i = 0; i < kNumStems; ++i)
                sum += stems[(size_t) i].channels[(size_t) c][(size_t) n];
            const double r = (double) x[n] - (double) sum;    // mix + (polaridade invertida da soma)
            resEnergy += r * r;
            mixEnergy += (double) x[n] * (double) x[n];
            peak = std::max (peak, std::abs (r));
        }
    }
    rep.residualPeakDbfs = peak > 0.0 ? 20.0 * std::log10 (peak) : -400.0;
    rep.residualRelDb = (resEnergy > 0.0 && mixEnergy > 0.0) ? 10.0 * std::log10 (resEnergy / mixEnergy) : -400.0;
    rep.valid = true;
    return rep;
}

} // namespace stemsep
