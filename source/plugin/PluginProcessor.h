#pragma once

#include <JuceHeader.h>

#include "SeparationService.h"
#include "StemARA.h"
#include "StemRendering.h"

namespace stemsep
{

class StemSepProcessor : public juce::AudioProcessor,
                         public juce::AudioProcessorARAExtension
{
public:
    StemSepProcessor();
    ~StemSepProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- usado pela interface ---------------------------------------------
    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }
    MixControls& getMixControls() { return controls; }
    SeparationService& getService() { return *service; }

    struct SourceInfo
    {
        SourceEntryPtr entry;
        juce::ARAAudioSource* araSource = nullptr;   // nulo no modo arquivo
    };
    // Fontes desta instancia (thread de mensagens): itens ARA ou o arquivo carregado.
    std::vector<SourceInfo> getSources() const;

    bool isARAMode() const noexcept { return isBoundToARA(); }

    // modo sem ARA: carrega um arquivo de audio e separa
    void loadFile (const juce::File& file);
    juce::File getLoadedFile() const { return loadedFile; }

    void reanalyse (const SourceInfo& src, bool force);

    double getHostSampleRate() const noexcept { return hostRate.load(); }

    static juce::String paramId (const char* base, int stem) { return juce::String (base) + juce::String (stem); }

protected:
    void didBindToARA() noexcept override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setFileEntry (SourceEntryPtr e);

    juce::AudioProcessorValueTreeState parameters;
    MixControls controls;
    juce::SharedResourcePointer<SeparationService> service;

    // modo arquivo (sem ARA)
    SourceEntryPtr fileEntry;
    std::atomic<SourceEntry*> fileEntryRT { nullptr };
    std::atomic<int> fileReaders { 0 };
    juce::File loadedFile;
    GainRamp fileRamp;

    std::atomic<double> hostRate { 44100.0 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (StemSepProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSepProcessor)
};

} // namespace stemsep
