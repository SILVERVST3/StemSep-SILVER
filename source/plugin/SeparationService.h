#pragma once
// Servico global (um por processo): threads de trabalho, modelo, cache em disco e configuracoes.

#include <JuceHeader.h>

#include <deque>
#include <functional>
#include <memory>
#include <mutex>

#include "../engine/Separator.h"
#include "StemData.h"

namespace stemsep
{

struct QualityPreset
{
    int overlap;
    bool tta;
    const char* name;
    const char* description;
};

QualityPreset qualityPreset (int quality);   // 0 = Rapida, 1 = Alta, 2 = Maxima

struct ServiceSettings
{
    int quality = 1;        // padrao: Alta
    int device = 0;         // 0 Auto (GPU se houver), 1 CPU, 2 GPU
    int threads = 0;        // 0 = automatico
    int exportBits = 32;    // 24 ou 32 (float)
};

// Fila simples executada num juce::Thread
class TaskThread : public juce::Thread
{
public:
    explicit TaskThread (const juce::String& name) : juce::Thread (name) {}
    ~TaskThread() override { stop(); }

    void add (std::function<void()> fn)
    {
        {
            const std::lock_guard<std::mutex> l (mutex);
            queue.push_back (std::move (fn));
        }
        notify();
    }

    void stop()
    {
        signalThreadShouldExit();
        notify();
        stopThread (15000);
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            std::function<void()> fn;
            {
                const std::lock_guard<std::mutex> l (mutex);
                if (! queue.empty())
                {
                    fn = std::move (queue.front());
                    queue.pop_front();
                }
            }
            if (fn)
                fn();
            else
                wait (500);
        }
    }

private:
    std::mutex mutex;
    std::deque<std::function<void()>> queue;
};

class SeparationService
{
public:
    // Executado no thread de E/S: deve preencher 'out' com o audio completo da fonte.
    using AudioProvider = std::function<bool (AudioData& out, juce::String& error)>;

    SeparationService();
    ~SeparationService();

    // --- configuracoes ------------------------------------------------------
    ServiceSettings getSettings() const;
    void setSettings (const ServiceSettings& s);

    // --- trabalho -----------------------------------------------------------
    // Le o audio (provider), procura no cache e, se preciso, separa. force = ignora o cache.
    void analyse (SourceEntryPtr entry, AudioProvider provider, bool force);

    // Cancela qualquer trabalho desta fonte (interrompe a inferencia se for a fonte atual).
    void cancel (SourceEntry& entry);

    // Gera (se necessario) a variante na taxa de reproducao 'rate'.
    void ensureRate (SourceEntryPtr entry, double rate);

    // Exporta os stems prontos (assincrono). onDone roda no thread de mensagens.
    void exportStems (SourceEntryPtr entry, juce::File folder, juce::String baseName,
                      std::function<void (bool ok, juce::String message, juce::Array<juce::File> files)> onDone);

    // Teste de nulidade sobre os dados em memoria: mix + polaridade invertida da soma dos stems.
    static NullReport nullTest (SourceEntry& entry);

    // --- caminhos -----------------------------------------------------------
    static juce::File getDataDirectory();
    static juce::File getCacheDirectory();
    static juce::File findModelFile();
    static juce::File findRuntimeFile();
    static juce::String modelFileName() { return "StemSep_SCNet_XL.onnx"; }

    juce::int64 getCacheSizeBytes() const;
    void clearCache();

    juce::String getLastDevice() const;

private:
    void runSeparation (SourceEntryPtr entry, int generation, std::shared_ptr<const AudioData> mix,
                        juce::String key, int quality);
    void makeVariant (SourceEntry& entry, double rate);

    static juce::String computeKey (const AudioData& mix, int quality);
    static bool loadFromCache (const juce::String& key, const AudioData& mix, StemArray& stems, NullReport& nr, juce::String& info);
    static bool storeToCache (const juce::String& key, const StemArray& stems, const NullReport& nr, const juce::String& info);

    bool shouldStop (const SourceEntry& e, int generation) const;

    mutable std::mutex settingsMutex;
    ServiceSettings settings;
    void loadSettings();
    void saveSettings() const;

    Separator separator;
    mutable std::mutex deviceMutex;
    juce::String lastDevice;

    std::atomic<bool> shuttingDown { false };
    std::atomic<SourceEntry*> runningEntry { nullptr };
    TaskThread ioThread { "StemSep IO" };
    TaskThread sepThread { "StemSep Separacao" };
};

// Escreve um AudioData como WAV (32 bits float ou 24 bits PCM)
bool writeWavFile (const juce::File& file, const AudioData& audio, int bits, juce::String& error);

} // namespace stemsep
