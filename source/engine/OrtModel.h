#pragma once
// Carrega o ONNX Runtime dinamicamente (onnxruntime.dll / libonnxruntime.so) a partir de um
// caminho absoluto. Assim o VST3 nao depende da ordem de busca de DLLs do Windows (o REAPER
// procura DLLs na pasta do reaper.exe, nao na pasta do plugin).

#include <memory>
#include <string>
#include <vector>

#include "StemTypes.h"

namespace stemsep
{

class OrtModel
{
public:
    OrtModel();
    ~OrtModel();

    OrtModel (const OrtModel&) = delete;
    OrtModel& operator= (const OrtModel&) = delete;

    // runtimeLibPath: caminho completo da onnxruntime.dll (Windows) ou libonnxruntime.so (Linux)
    // modelPath: arquivo .onnx do SCNet exportado por tools/export_scnet_onnx.py
    bool load (const std::string& runtimeLibPath, const std::string& modelPath,
               Device device, int numThreads, std::string& error);

    bool isLoaded() const noexcept;
    std::string deviceDescription() const;

    // input  [1, 4, 2049, 476]  -> output [1, 16, 2049, 476]
    bool run (const float* input, float* output, std::string& error);

    // Interrompe a inferencia em andamento (pode ser chamado de outro thread).
    void abortRun();
    void resetAbort();

    static constexpr int kInChannels  = 4;
    static constexpr int kOutChannels = 16;
    static constexpr int kFreqs       = 2049;
    static constexpr int kFrames      = 476;

    static constexpr size_t inputSize()  noexcept { return (size_t) kInChannels  * kFreqs * kFrames; }
    static constexpr size_t outputSize() noexcept { return (size_t) kOutChannels * kFreqs * kFrames; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace stemsep
