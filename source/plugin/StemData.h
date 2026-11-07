#pragma once
// Dados compartilhados entre os threads de trabalho, o thread de audio e a interface.

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>

#include "../engine/StemTypes.h"

namespace stemsep
{

using StemArray = std::array<AudioData, kNumStems>;

// Conjunto imutavel (depois de publicado) de mix + 4 stems numa determinada taxa de amostragem.
struct StemData
{
    double sampleRate = 44100.0;
    int numChannels = 0;
    int64_t numSamples = 0;
    std::shared_ptr<const AudioData> mix;      // sempre presente
    std::shared_ptr<const StemArray> stems;    // nulo enquanto a separacao nao terminou
    NullReport nullTest;

    bool hasStems() const noexcept { return stems != nullptr; }
};

enum class EntryState : int
{
    Idle = 0,
    Reading,
    Waiting,       // na fila de separacao
    Loading,       // carregando modelo
    Separating,
    Ready,
    Error
};

// Estado de uma fonte de audio (item do REAPER via ARA, ou arquivo no modo sem ARA).
// Publicacao sem bloqueio para o thread de audio: ponteiros atomicos + contador de leitores.
class SourceEntry
{
public:
    static constexpr int kNumSlots = 2;   // [0] taxa original, [1] variante na taxa do projeto

    SourceEntry()
    {
        for (auto& s : slots)
            s.store (nullptr);
    }

    ~SourceEntry()
    {
        for (auto& s : slots)
            delete s.exchange (nullptr);
    }

    // --- thread de audio --------------------------------------------------
    class ReadScope
    {
    public:
        explicit ReadScope (SourceEntry& e) noexcept : entry (e) { entry.readers.fetch_add (1); }
        ~ReadScope() noexcept { entry.readers.fetch_sub (1); }

        const StemData* find (double rate) const noexcept
        {
            for (auto& s : entry.slots)
                if (auto* d = s.load())
                    if (std::abs (d->sampleRate - rate) < 0.5)
                        return d;
            return nullptr;
        }

    private:
        SourceEntry& entry;
        JUCE_DECLARE_NON_COPYABLE (ReadScope)
    };

    // --- threads de trabalho (segurando workMutex) ------------------------
    void publish (int slot, std::unique_ptr<StemData> data)
    {
        StemData* old = slots[(size_t) slot].exchange (data.release());
        waitForReaders();
        delete old;
    }

    void clearSlots()
    {
        std::array<StemData*, kNumSlots> old {};
        for (int i = 0; i < kNumSlots; ++i)
            old[(size_t) i] = slots[(size_t) i].exchange (nullptr);
        waitForReaders();
        for (auto* p : old)
            delete p;
    }

    // leitura fora do thread de audio: segure workMutex enquanto usa o ponteiro
    const StemData* peek (int slot) const noexcept { return slots[(size_t) slot].load(); }

    std::mutex workMutex;

    // estado para a UI
    std::atomic<int> state { (int) EntryState::Idle };
    std::atomic<float> progress { 0.0f };
    std::atomic<int> generation { 0 };          // muda a cada nova analise (cancela jobs antigos)
    std::atomic<bool> alive { true };
    std::atomic<double> requestedRate { 0.0 };  // taxa pedida pelo renderizador (0 = nenhuma)

    EntryState getState() const noexcept { return (EntryState) state.load(); }
    void setState (EntryState s) noexcept { state.store ((int) s); }

    void setStatus (const juce::String& s)  { const std::lock_guard<std::mutex> l (textMutex); statusText = s; }
    juce::String getStatus() const          { const std::lock_guard<std::mutex> l (textMutex); return statusText; }
    void setName (const juce::String& s)    { const std::lock_guard<std::mutex> l (textMutex); name = s; }
    juce::String getName() const            { const std::lock_guard<std::mutex> l (textMutex); return name; }
    void setInfo (const juce::String& s)    { const std::lock_guard<std::mutex> l (textMutex); info = s; }
    juce::String getInfo() const            { const std::lock_guard<std::mutex> l (textMutex); return info; }

private:
    void waitForReaders() const
    {
        while (readers.load() != 0)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    std::array<std::atomic<StemData*>, kNumSlots> slots;
    mutable std::atomic<int> readers { 0 };
    mutable std::mutex textMutex;
    juce::String statusText { juce::String::fromUTF8 ("Aguardando áudio") };
    juce::String name;
    juce::String info;
};

using SourceEntryPtr = std::shared_ptr<SourceEntry>;

} // namespace stemsep
