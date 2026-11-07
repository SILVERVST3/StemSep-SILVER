#include "Separator.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "MixConsistency.h"
#include "ModelStft.h"
#include "Resample.h"

namespace stemsep
{

namespace
{
    constexpr int kScnetPadding = 1300;                        // padding interno do SCNet p/ 485100
    constexpr int kPaddedChunk  = Separator::kChunk + kScnetPadding;   // 486400 = 475 * 1024
    constexpr int kF = OrtModel::kFreqs;
    constexpr int kT = OrtModel::kFrames;

    static_assert (kPaddedChunk % ModelStft::kHop == 0, "chunk deve ser multiplo do hop");
    static_assert (kPaddedChunk / ModelStft::kHop + 1 == OrtModel::kFrames, "quadros do modelo");

    struct DemixPlan
    {
        int64_t chunk, step, fade, border, n;
        bool padded;
    };

    DemixPlan makePlan (int64_t n0, int overlap)
    {
        DemixPlan p;
        p.chunk = Separator::kChunk;
        p.step = p.chunk / std::max (1, overlap);
        p.fade = p.chunk / 10;
        p.border = p.chunk - p.step;
        p.padded = n0 > 2 * p.border && p.border > 0;
        p.n = p.padded ? n0 + 2 * p.border : n0;
        return p;
    }

    int64_t chunksFor (const DemixPlan& p)
    {
        return (p.n + p.step - 1) / p.step;
    }

    // numpy.pad(..., mode='reflect') de um lado so (pad < len)
    inline float reflectIndex (const float* x, int64_t len, int64_t j)
    {
        if (j < 0) j = -j;
        if (j >= len) j = 2 * (len - 1) - j;
        j = std::max<int64_t> (0, std::min<int64_t> (len - 1, j));
        return x[j];
    }
}

Separator::Separator()
{
    chunkIn[0].assign ((size_t) kPaddedChunk, 0.0f);
    chunkIn[1].assign ((size_t) kPaddedChunk, 0.0f);
}

Separator::~Separator() = default;

bool Separator::prepare (const std::string& runtimeLibPath, const std::string& modelPath,
                         Device device, int numThreads, std::string& error)
{
    std::lock_guard<std::mutex> lock (mutex);
    if (model.isLoaded() && modelPath == loadedModelPath && device == loadedDevice && numThreads == loadedThreads)
        return true;
    if (! model.load (runtimeLibPath, modelPath, device, numThreads, error))
        return false;
    loadedModelPath = modelPath;
    loadedDevice = device;
    loadedThreads = numThreads;
    return true;
}

bool Separator::isReady() const noexcept { return model.isLoaded(); }
std::string Separator::deviceDescription() const { return model.deviceDescription(); }

int64_t Separator::countChunks (int64_t numSamples, double sampleRate, const SeparationOptions& opts)
{
    const int64_t n44 = std::abs (sampleRate - kModelRate) < 1e-9 ? numSamples
                                                                 : resampledLength (numSamples, sampleRate, kModelRate);
    return chunksFor (makePlan (n44, opts.overlap)) * (opts.tta ? 3 : 1);
}

bool Separator::runChunk (const float* left, const float* right, std::string& error)
{
    // bloco + padding de zeros do SCNet
    const float* src[2] { left, right };
    for (int c = 0; c < 2; ++c)
    {
        std::copy (src[c], src[c] + kChunk, chunkIn[c].begin());
        std::fill (chunkIn[c].begin() + kChunk, chunkIn[c].end(), 0.0f);
    }

    specIn.resize (OrtModel::inputSize());
    specOut.resize (OrtModel::outputSize());
    for (int c = 0; c < 2; ++c)
    {
        float* re = specIn.data() + (size_t) (c * 2 + 0) * kF * kT;
        float* im = specIn.data() + (size_t) (c * 2 + 1) * kF * kT;
        ModelStft::forward (chunkIn[c].data(), kPaddedChunk, re, im, kT);
    }

    if (! model.run (specIn.data(), specOut.data(), error))
        return false;

    chunkOut.resize ((size_t) kNumStems * 2 * kChunk);
    std::vector<float> full ((size_t) kPaddedChunk);
    for (int s = 0; s < kNumStems; ++s)
    {
        for (int c = 0; c < 2; ++c)
        {
            const size_t ch = (size_t) (4 * s + 2 * c);
            const float* re = specOut.data() + (ch + 0) * kF * kT;
            const float* im = specOut.data() + (ch + 1) * kF * kT;
            ModelStft::inverse (re, im, kT, kT, full.data(), kPaddedChunk);
            std::copy (full.begin(), full.begin() + kChunk, chunkOut.begin() + (size_t) (s * 2 + c) * kChunk);
        }
    }

    // protege contra NaN/Inf (igual ao np.nan_to_num do MSST)
    for (auto& v : chunkOut)
        if (! std::isfinite (v)) v = 0.0f;
    return true;
}

bool Separator::demix (const std::vector<float>& left, const std::vector<float>& right,
                       int overlap, std::array<std::array<std::vector<float>, 2>, kNumStems>& out,
                       const std::function<bool (int64_t)>& onChunk, std::string& error)
{
    const int64_t n0 = (int64_t) left.size();
    const DemixPlan plan = makePlan (n0, overlap);
    const int64_t n = plan.n;

    // mix com padding refletido nas bordas (so para audios longos, como no MSST)
    std::vector<float> padded[2];
    const float* mixPtr[2] { left.data(), right.data() };
    if (plan.padded)
    {
        for (int c = 0; c < 2; ++c)
        {
            padded[c].resize ((size_t) n);
            for (int64_t i = 0; i < n; ++i)
                padded[c][(size_t) i] = reflectIndex (mixPtr[c], n0, i - plan.border);
            mixPtr[c] = padded[c].data();
        }
    }

    for (auto& st : out)
        for (auto& ch : st)
            ch.assign ((size_t) n, 0.0f);
    std::vector<float> counter ((size_t) n, 0.0f);

    std::vector<float> baseWindow ((size_t) plan.chunk, 1.0f);
    for (int64_t k = 0; k < plan.fade; ++k)
    {
        const float v = plan.fade > 1 ? (float) k / (float) (plan.fade - 1) : 1.0f;   // linspace(0, 1)
        baseWindow[(size_t) k] = v;
        baseWindow[(size_t) (plan.chunk - 1 - k)] = v;                                 // linspace(1, 0)
    }

    std::vector<float> part[2];
    part[0].resize ((size_t) plan.chunk);
    part[1].resize ((size_t) plan.chunk);
    std::vector<float> window ((size_t) plan.chunk);

    int64_t done = 0;
    for (int64_t i = 0; i < n; i += plan.step)
    {
        const int64_t clen = std::min<int64_t> (plan.chunk, n - i);
        const bool reflectPad = clen > plan.chunk / 2;
        for (int c = 0; c < 2; ++c)
        {
            const float* src = mixPtr[c] + i;
            std::copy (src, src + clen, part[c].begin());
            for (int64_t j = clen; j < plan.chunk; ++j)
                part[c][(size_t) j] = reflectPad ? reflectIndex (src, clen, j) : 0.0f;
        }

        if (! runChunk (part[0].data(), part[1].data(), error))
            return false;

        window = baseWindow;
        if (i == 0)
            std::fill (window.begin(), window.begin() + plan.fade, 1.0f);
        else if (i + plan.step >= n)
            std::fill (window.end() - plan.fade, window.end(), 1.0f);

        for (int s = 0; s < kNumStems; ++s)
            for (int c = 0; c < 2; ++c)
            {
                const float* y = chunkOut.data() + (size_t) (s * 2 + c) * plan.chunk;
                float* dst = out[(size_t) s][(size_t) c].data() + i;
                for (int64_t k = 0; k < clen; ++k)
                    dst[k] += y[k] * window[(size_t) k];
            }
        for (int64_t k = 0; k < clen; ++k)
            counter[(size_t) (i + k)] += window[(size_t) k];

        ++done;
        if (onChunk && ! onChunk (done))
        {
            error = "cancelado";
            return false;
        }
    }

    for (auto& st : out)
        for (auto& ch : st)
        {
            for (int64_t k = 0; k < n; ++k)
            {
                const float w = counter[(size_t) k];
                ch[(size_t) k] = w > 1e-12f ? ch[(size_t) k] / w : 0.0f;
            }
            if (plan.padded)
            {
                ch.erase (ch.begin() + (n - plan.border), ch.end());
                ch.erase (ch.begin(), ch.begin() + plan.border);
            }
            ch.shrink_to_fit();
        }
    return true;
}

bool Separator::separate (const AudioData& mix, const SeparationOptions& opts, SeparationResult& result,
                          const ProgressFn& progress, const std::function<bool()>& shouldCancel, std::string& error)
{
    std::lock_guard<std::mutex> lock (mutex);
    const auto t0 = std::chrono::steady_clock::now();

    if (! model.isLoaded())
    {
        error = "modelo nao carregado";
        return false;
    }
    model.resetAbort();
    if (shouldCancel && shouldCancel())
    {
        error = "cancelado";
        return false;
    }
    const int inCh = mix.numChannels();
    const int64_t ns = mix.numSamples();
    if (inCh < 1 || ns < 2048)
    {
        error = "audio vazio ou curto demais";
        return false;
    }
    const double sr = mix.sampleRate;
    const bool needResample = std::abs (sr - kModelRate) > 1e-6;
    const int outCh = std::min (inCh, 2);

    if (progress) progress (0.0, needResample ? "Convertendo para 44,1 kHz" : "Preparando");

    // 1) estereo 44.1 kHz
    std::vector<float> L, R;
    {
        const float* src0 = mix.channels[0].data();
        const float* src1 = inCh > 1 ? mix.channels[1].data() : mix.channels[0].data();
        if (needResample)
        {
            const int64_t n44 = resampledLength (ns, sr, kModelRate);
            L.resize ((size_t) n44);
            R.resize ((size_t) n44);
            resampleChannel (src0, ns, sr, L.data(), n44, kModelRate);
            if (inCh > 1) resampleChannel (src1, ns, sr, R.data(), n44, kModelRate);
            else R = L;
        }
        else
        {
            L.assign (src0, src0 + ns);
            R.assign (src1, src1 + ns);
        }
    }

    // 2) separacao (+ TTA)
    const int passes = opts.tta ? 3 : 1;
    const int64_t perPass = chunksFor (makePlan ((int64_t) L.size(), opts.overlap));
    const int64_t totalChunks = perPass * passes;
    int64_t chunksDone = 0;

    auto makeCallback = [&] (int passIndex)
    {
        return [&, passIndex] (int64_t doneInPass) -> bool
        {
            if (shouldCancel && shouldCancel())
                return false;
            const int64_t d = chunksDone + doneInPass;
            if (progress)
            {
                const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                const double frac = (double) d / (double) totalChunks;
                const double eta = frac > 0.0 ? elapsed * (1.0 - frac) / frac : 0.0;
                std::string s = "Separando";
                if (passes > 1) s += " (passada " + std::to_string (passIndex + 1) + "/3)";
                s += " - faltam ~" + std::to_string ((int) std::ceil (eta / 60.0 - 1e-9)) + " min";
                progress (0.02 + 0.93 * frac, s);
            }
            return true;
        };
    };

    std::array<std::array<std::vector<float>, 2>, kNumStems> acc;
    if (! demix (L, R, opts.overlap, acc, makeCallback (0), error))
        return false;
    chunksDone += perPass;

    if (opts.tta)
    {
        std::array<std::array<std::vector<float>, 2>, kNumStems> tmp;
        // canais trocados
        if (! demix (R, L, opts.overlap, tmp, makeCallback (1), error))
            return false;
        chunksDone += perPass;
        for (int s = 0; s < kNumStems; ++s)
            for (int c = 0; c < 2; ++c)
            {
                auto& a = acc[(size_t) s][(size_t) c];
                const auto& b = tmp[(size_t) s][(size_t) (1 - c)];
                for (size_t k = 0; k < a.size(); ++k) a[k] += b[k];
            }
        // polaridade invertida
        for (auto& v : L) v = -v;
        for (auto& v : R) v = -v;
        if (! demix (L, R, opts.overlap, tmp, makeCallback (2), error))
            return false;
        chunksDone += perPass;
        for (int s = 0; s < kNumStems; ++s)
            for (int c = 0; c < 2; ++c)
            {
                auto& a = acc[(size_t) s][(size_t) c];
                const auto& b = tmp[(size_t) s][(size_t) c];
                for (size_t k = 0; k < a.size(); ++k) a[k] = (a[k] - b[k]) / 3.0f;
            }
    }
    L.clear(); L.shrink_to_fit();
    R.clear(); R.shrink_to_fit();

    if (progress) progress (0.95, "Finalizando stems");

    // 3) volta para a taxa e canais originais
    for (int s = 0; s < kNumStems; ++s)
    {
        AudioData& dst = result.stems[(size_t) s];
        dst.sampleRate = sr;
        dst.resize (outCh, ns);
        std::vector<float> tmpCh;
        for (int c = 0; c < 2; ++c)
        {
            auto& src = acc[(size_t) s][(size_t) c];
            const float* p = src.data();
            if (needResample)
            {
                tmpCh.resize ((size_t) ns);
                resampleChannel (src.data(), (int64_t) src.size(), kModelRate, tmpCh.data(), ns, sr);
                p = tmpCh.data();
            }
            const int64_t avail = needResample ? ns : std::min<int64_t> (ns, (int64_t) src.size());
            if (outCh == 1)
            {
                auto& d = dst.channels[0];
                for (int64_t k = 0; k < avail; ++k) d[(size_t) k] += 0.5f * p[k];
            }
            else
            {
                std::copy (p, p + avail, dst.channels[(size_t) c].begin());
            }
            src.clear();
            src.shrink_to_fit();
        }
    }

    // 4) consistencia de mixagem (nulidade)
    AudioData mixRef;
    mixRef.sampleRate = sr;
    mixRef.channels.assign (mix.channels.begin(), mix.channels.begin() + outCh);
    if (opts.mixConsistency)
    {
        if (progress) progress (0.97, "Consistência de mixagem");
        for (int c = 0; c < outCh; ++c)
        {
            std::array<float*, kNumStems> ptrs {};
            for (int s = 0; s < kNumStems; ++s)
                ptrs[(size_t) s] = result.stems[(size_t) s].channels[(size_t) c].data();
            applyMixConsistency (mixRef.channels[(size_t) c].data(), ns, ptrs);
        }
    }

    result.nullTest = measureNull (mixRef, result.stems);
    result.deviceUsed = model.deviceDescription();
    result.secondsElapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    if (progress) progress (1.0, "Pronto");
    return true;
}

} // namespace stemsep
