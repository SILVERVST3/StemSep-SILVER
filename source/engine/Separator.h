#pragma once
// StemSep SILVER - motor de separacao em 4 stems (SCNet XL via ONNX Runtime).
//
// Fluxo:  mix (qualquer taxa / mono ou estereo)
//          -> estereo 44.1 kHz (r8brain)
//          -> blocos de 11 s com sobreposicao (2x ou 4x) e crossfade linear (igual ao MSST)
//          -> por bloco: STFT (C++) -> SCNet (ONNX) -> iSTFT (C++)
//          -> [opcional] TTA: media com canais trocados e polaridade invertida
//          -> volta para a taxa/canais originais
//          -> consistencia de mixagem (soma dos stems == mix)

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "OrtModel.h"
#include "StemTypes.h"

namespace stemsep
{

class Separator
{
public:
    static constexpr double kModelRate = 44100.0;
    static constexpr int    kChunk     = 485100;   // 11 s

    Separator();
    ~Separator();

    // Carrega (ou recarrega, se device/threads mudarem) o modelo. Pode demorar alguns segundos.
    bool prepare (const std::string& runtimeLibPath, const std::string& modelPath,
                  Device device, int numThreads, std::string& error);

    bool isReady() const noexcept;

    // Interrompe a separacao em andamento o mais rapido possivel (qualquer thread).
    void abort() { model.abortRun(); }
    std::string deviceDescription() const;

    // Numero de passadas do modelo (para estimar tempo).
    static int64_t countChunks (int64_t numSamples, double sampleRate, const SeparationOptions& opts);

    // Separacao completa. 'cancel' pode ser acionado de outra thread.
    bool separate (const AudioData& mix, const SeparationOptions& opts, SeparationResult& result,
                   const ProgressFn& progress, const std::function<bool()>& shouldCancel, std::string& error);

private:
    bool demix (const std::vector<float>& left, const std::vector<float>& right,
                int overlap, std::array<std::array<std::vector<float>, 2>, kNumStems>& out,
                const std::function<bool (int64_t done)>& onChunk, std::string& error);

    bool runChunk (const float* left, const float* right, std::string& error);

    OrtModel model;
    std::string loadedModelPath;
    Device loadedDevice = Device::Auto;
    int loadedThreads = -1;
    std::mutex mutex;

    std::vector<float> chunkIn[2];     // bloco de entrada (com padding do SCNet)
    std::vector<float> specIn;         // [4, 2049, 476]
    std::vector<float> specOut;        // [16, 2049, 476]
    std::vector<float> chunkOut;       // [4 fontes][2 canais][kChunk]
};

} // namespace stemsep
