#pragma once
// Integracao ARA2 (REAPER, Studio One, Cubase...): o plugin le o item inteiro da timeline,
// separa em segundo plano e toca os stems sincronizados com o projeto.

#include <JuceHeader.h>

#include <map>

#include "SeparationService.h"
#include "StemRendering.h"

namespace stemsep
{

class StemDocumentController : public juce::ARADocumentControllerSpecialisation
{
public:
    using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

    // thread de mensagens (ou thread de audio segurando getProcessingLock())
    SourceEntryPtr findEntry (juce::ARAAudioSource* source) const;

    juce::ScopedTryReadLock getProcessingLock() { return juce::ScopedTryReadLock (processingLock); }

    // thread de mensagens
    void reanalyse (juce::ARAAudioSource* source, bool force);
    void setPlaybackRate (double rate);

    SeparationService& getService() { return *service; }

protected:
    void willBeginEditing (juce::ARADocument*) override;
    void didEndEditing (juce::ARADocument*) override;

    void didAddAudioSourceToDocument (juce::ARADocument*, juce::ARAAudioSource*) override;
    void willDestroyAudioSource (juce::ARAAudioSource*) override;
    void didEnableAudioSourceSamplesAccess (juce::ARAAudioSource*, bool enable) override;
    void doUpdateAudioSourceContent (juce::ARAAudioSource*, juce::ARAContentUpdateScopes) override;
    void didUpdateAudioSourceProperties (juce::ARAAudioSource*) override;

    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override;

    bool doRestoreObjectsFromStream (juce::ARAInputStream& input, const juce::ARARestoreObjectsFilter* filter) noexcept override;
    bool doStoreObjectsToStream (juce::ARAOutputStream& output, const juce::ARAStoreObjectsFilter* filter) noexcept override;

private:
    void startIfPossible (juce::ARAAudioSource* source, bool force);

    juce::SharedResourcePointer<SeparationService> service;
    juce::ReadWriteLock processingLock;
    std::map<juce::ARAAudioSource*, SourceEntryPtr> entries;
    std::vector<juce::ARAAudioSource*> pendingStarts;
    std::atomic<double> playbackRate { 0.0 };
    bool editing = false;
};

class StemPlaybackRenderer : public juce::ARAPlaybackRenderer
{
public:
    using juce::ARAPlaybackRenderer::ARAPlaybackRenderer;

    void setControls (MixControls* c) noexcept { controls.store (c); }

    void prepareToPlay (double sampleRate, int maximumSamplesPerBlock, int numChannels,
                        juce::AudioProcessor::ProcessingPrecision, AlwaysNonRealtime) override;
    void releaseResources() override;

    bool processBlock (juce::AudioBuffer<float>& buffer, juce::AudioProcessor::Realtime realtime,
                       const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;

    using juce::ARAPlaybackRenderer::processBlock;

    StemDocumentController* getStemDocumentController() const noexcept;

    // fontes de audio tocadas por esta instancia (thread de mensagens)
    std::vector<juce::ARAAudioSource*> getAudioSources() const;

private:
    void waitForNonRealtime (const juce::AudioPlayHead::PositionInfo& positionInfo, int numSamples);

    std::atomic<MixControls*> controls { nullptr };
    GainRamp ramp;
    double sampleRate = 44100.0;
    int maxBlock = 512;
};

} // namespace stemsep
