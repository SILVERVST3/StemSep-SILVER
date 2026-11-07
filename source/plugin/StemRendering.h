#pragma once
// Mixagem dos stems no thread de audio (sem alocacao) + controles de mixagem.

#include <JuceHeader.h>

#include "StemData.h"

namespace stemsep
{

// Controles de mixagem por instancia (ligados aos parametros do plugin).
struct MixControls
{
    std::array<std::atomic<float>*, kNumStems> gainDb {};
    std::array<std::atomic<float>*, kNumStems> mute {};
    std::array<std::atomic<float>*, kNumStems> solo {};
    std::array<std::atomic<float>, kNumStems> meters {};   // pico por stem (UI zera ao ler)

    MixControls()
    {
        for (auto& m : meters)
            m.store (0.0f);
    }

    void computeTargets (float* out) const noexcept
    {
        bool anySolo = false;
        for (int s = 0; s < kNumStems; ++s)
            if (solo[(size_t) s] != nullptr && solo[(size_t) s]->load() > 0.5f)
                anySolo = true;
        for (int s = 0; s < kNumStems; ++s)
        {
            const float db = gainDb[(size_t) s] != nullptr ? gainDb[(size_t) s]->load() : 0.0f;
            float g = db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db);
            if (mute[(size_t) s] != nullptr && mute[(size_t) s]->load() > 0.5f) g = 0.0f;
            if (anySolo && ! (solo[(size_t) s] != nullptr && solo[(size_t) s]->load() > 0.5f)) g = 0.0f;
            out[s] = g;
        }
    }

    void pushMeter (int stem, float peak) noexcept
    {
        auto& m = meters[(size_t) stem];
        float cur = m.load();
        while (peak > cur && ! m.compare_exchange_weak (cur, peak)) {}
    }
};

// Rampa de ganho por bloco (evita cliques ao mexer em volume / mute / solo).
struct GainRamp
{
    std::array<float, kNumStems> current { 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, kNumStems> target  { 1.0f, 1.0f, 1.0f, 1.0f };
    bool initialised = false;

    void setTargets (const float* t) noexcept
    {
        for (int s = 0; s < kNumStems; ++s)
            target[(size_t) s] = t[s];
        if (! initialised)
        {
            current = target;
            initialised = true;
        }
    }

    void advance() noexcept { current = target; }
};

// Soma em 'out' (a partir de outStart, n amostras) o conteudo de 'data' a partir de srcStart.
// Amostras fora de [0, numSamples) sao ignoradas. blockOffset/blockLength definem a rampa de ganho.
inline void renderStemData (const StemData& data, int64_t srcStart, juce::AudioBuffer<float>& out,
                            int outStart, int n, const GainRamp& ramp, int blockLength,
                            MixControls* controls) noexcept
{
    // recorta para a parte valida da fonte
    int64_t s0 = srcStart;
    int o0 = outStart;
    int count = n;
    if (s0 < 0)
    {
        const int skip = (int) std::min<int64_t> (count, -s0);
        s0 += skip;
        o0 += skip;
        count -= skip;
    }
    if (s0 + count > data.numSamples)
        count = (int) std::max<int64_t> (0, data.numSamples - s0);
    if (count <= 0)
        return;

    const int outCh = out.getNumChannels();
    const int srcCh = data.numChannels;
    const float invBlock = blockLength > 0 ? 1.0f / (float) blockLength : 0.0f;

    auto addSignal = [&] (const AudioData& sig, float g0, float g1, int stemIndex)
    {
        const bool constant = std::abs (g1 - g0) < 1.0e-7f;
        if (constant && std::abs (g0) < 1.0e-9f)
            return;
        float peak = 0.0f;
        for (int c = 0; c < outCh; ++c)
        {
            float* dst = out.getWritePointer (c, o0);
            const int sc = std::min (c, srcCh - 1);
            const float* src = sig.channels[(size_t) sc].data() + s0;
            const bool downmix = (outCh == 1 && srcCh > 1);
            for (int k = 0; k < count; ++k)
            {
                const float g = constant ? g0 : g0 + (g1 - g0) * (float) (o0 + k) * invBlock;
                float v = src[k];
                if (downmix)
                    v = 0.5f * (v + sig.channels[1][(size_t) (s0 + k)]);
                v *= g;
                dst[k] += v;
                peak = std::max (peak, std::abs (v));
            }
        }
        if (controls != nullptr && stemIndex >= 0)
            controls->pushMeter (stemIndex, peak);
    };

    if (data.hasStems())
    {
        for (int s = 0; s < kNumStems; ++s)
            addSignal ((*data.stems)[(size_t) s], ramp.current[(size_t) s], ramp.target[(size_t) s], s);
    }
    else if (data.mix != nullptr)
    {
        // separacao em andamento: toca o original
        addSignal (*data.mix, 1.0f, 1.0f, -1);
    }
}

} // namespace stemsep
