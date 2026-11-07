#pragma once
// StemSep SILVER - tipos basicos do motor de separacao (sem dependencia do JUCE).

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace stemsep
{

// Ordem de saida do modelo SCNet (igual ao MUSDB18): drums, bass, other, vocals.
enum StemIndex : int
{
    kDrums  = 0,
    kBass   = 1,
    kOther  = 2,   // "Instrumentos" (guitarras, teclados, sopros, etc.)
    kVocals = 3,
    kNumStems = 4
};

// Ordem de exibicao pedida: Bateria, Baixo, Vocal, Instrumentos.
constexpr std::array<int, kNumStems> kDisplayOrder { kDrums, kBass, kVocals, kOther };

inline const char* stemFileName (int stem)
{
    switch (stem)
    {
        case kDrums:  return "Bateria";
        case kBass:   return "Baixo";
        case kVocals: return "Vocal";
        default:      return "Instrumentos";
    }
}

// Audio planar: channels[c][n]
struct AudioData
{
    double sampleRate = 44100.0;
    std::vector<std::vector<float>> channels;

    int numChannels() const noexcept { return (int) channels.size(); }
    int64_t numSamples() const noexcept { return channels.empty() ? 0 : (int64_t) channels[0].size(); }

    void resize (int numCh, int64_t numSmp)
    {
        channels.assign ((size_t) numCh, std::vector<float> ((size_t) numSmp, 0.0f));
    }
};

enum class Device
{
    Auto,   // tenta GPU (DirectML no Windows) e cai para CPU se falhar
    Cpu,
    Gpu
};

struct SeparationOptions
{
    int  overlap         = 4;      // 2 = rapido, 4 = alta qualidade (padrao MSST)
    bool tta             = false;  // test-time augmentation (inverte canais e polaridade) = 3x mais lento
    bool mixConsistency  = true;   // redistribui o residuo => soma dos stems == mix (nulidade)
};

struct NullReport
{
    double residualPeakDbfs = -400.0;  // pico de |mix - soma(stems)|
    double residualRelDb    = -400.0;  // energia do residuo relativa ao mix
    bool   valid            = false;
};

struct SeparationResult
{
    std::array<AudioData, kNumStems> stems;
    NullReport nullTest;
    double secondsElapsed = 0.0;
    std::string deviceUsed;
};

// progresso: fracao 0..1 e texto curto de status
using ProgressFn = std::function<void (double fraction, const std::string& status)>;

} // namespace stemsep
