#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace stemsep
{

static const char* kStemParamNames[kNumStems] { "Bateria", "Baixo", "Instrumentos", "Vocal" };

juce::AudioProcessorValueTreeState::ParameterLayout StemSepProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int s : kDisplayOrder)
    {
        const juce::String name (kStemParamNames[s]);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { paramId ("gain", s), 1 }, name + " Volume",
            juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 2.5f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return v <= -59.9f ? juce::String ("-inf") : juce::String (v, 1); })));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { paramId ("mute", s), 1 }, name + " Mute", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { paramId ("solo", s), 1 }, name + " Solo", false));
    }
    return layout;
}

StemSepProcessor::StemSepProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "StemSepSILVER", createLayout())
{
    for (int s = 0; s < kNumStems; ++s)
    {
        controls.gainDb[(size_t) s] = parameters.getRawParameterValue (paramId ("gain", s));
        controls.mute[(size_t) s]   = parameters.getRawParameterValue (paramId ("mute", s));
        controls.solo[(size_t) s]   = parameters.getRawParameterValue (paramId ("solo", s));
    }
}

StemSepProcessor::~StemSepProcessor()
{
    setFileEntry (nullptr);
}

void StemSepProcessor::didBindToARA() noexcept
{
    juce::AudioProcessorARAExtension::didBindToARA();
    if (auto* r = getPlaybackRenderer<StemPlaybackRenderer>())
        r->setControls (&controls);
}

double StemSepProcessor::getTailLengthSeconds() const
{
    double tail = 0.0;
    if (getTailLengthSecondsForARA (tail))
        return tail;
    return 0.0;
}

bool StemSepProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == out;
}

void StemSepProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate;
    fileRamp.initialised = false;
    if (prepareToPlayForARA (sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision()))
        return;
    if (fileEntry != nullptr)
        service->ensureRate (fileEntry, sampleRate);
}

void StemSepProcessor::releaseResources()
{
    releaseResourcesForARA();
}

void StemSepProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    if (isBoundToARA())
    {
        processBlockForARA (buffer, isNonRealtime() ? juce::AudioProcessor::Realtime::no
                                                    : juce::AudioProcessor::Realtime::yes,
                            getPlayHead());
        return;
    }

    // ---- modo arquivo (host sem ARA): toca os stems do arquivo alinhados ao inicio do projeto
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    fileReaders.fetch_add (1);
    SourceEntry* e = fileEntryRT.load();
    if (e != nullptr)
    {
        const int n = buffer.getNumSamples();
        juce::Optional<juce::AudioPlayHead::PositionInfo> pos;
        if (auto* ph = getPlayHead())
            pos = ph->getPosition();
        buffer.clear();

        if (pos.hasValue() && pos->getIsPlaying())
        {
            if (isNonRealtime() && ! juce::MessageManager::existsAndIsCurrentThread())
            {
                // render offline: espera os stems ficarem prontos
                const auto start = juce::Time::getMillisecondCounterHiRes();
                while (juce::Time::getMillisecondCounterHiRes() - start < 4.0 * 3600.0 * 1000.0)
                {
                    const auto st = e->getState();
                    if (st == EntryState::Error || st == EntryState::Idle)
                        break;
                    if (st == EntryState::Ready)
                    {
                        SourceEntry::ReadScope rs (*e);
                        if (auto* d = rs.find (hostRate.load()))
                            if (d->hasStems())
                                break;
                    }
                    juce::Thread::sleep (50);
                }
            }

            float targets[kNumStems];
            controls.computeTargets (targets);
            fileRamp.setTargets (targets);
            SourceEntry::ReadScope rs (*e);
            if (const StemData* d = rs.find (hostRate.load()))
                renderStemData (*d, pos->getTimeInSamples().orFallback (0), buffer, 0, n, fileRamp, n, &controls);
            fileRamp.advance();
        }
    }
    fileReaders.fetch_sub (1);
}

std::vector<StemSepProcessor::SourceInfo> StemSepProcessor::getSources() const
{
    std::vector<SourceInfo> result;
    if (isBoundToARA())
    {
        if (auto* r = getPlaybackRenderer<StemPlaybackRenderer>())
            if (auto* dc = r->getStemDocumentController())
                for (auto* src : r->getAudioSources())
                    if (auto e = dc->findEntry (src))
                        result.push_back ({ e, src });
    }
    else if (fileEntry != nullptr)
    {
        result.push_back ({ fileEntry, nullptr });
    }
    return result;
}

void StemSepProcessor::setFileEntry (SourceEntryPtr e)
{
    SourceEntry* old = fileEntryRT.exchange (e.get());
    while (fileReaders.load() != 0)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    if (old != nullptr && fileEntry != nullptr)
    {
        fileEntry->alive = false;
        service->cancel (*fileEntry);
    }
    fileEntry = std::move (e);
}

namespace
{
    SeparationService::AudioProvider makeFileProvider (const juce::File& file)
    {
        return [file] (AudioData& out, juce::String& error) -> bool
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
            if (r == nullptr)
            {
                error = juce::String::fromUTF8 ("formato não suportado: ") + file.getFileName();
                return false;
            }
            const int ch = (int) std::min<unsigned int> (r->numChannels, 2);
            const juce::int64 len = r->lengthInSamples;
            out.sampleRate = r->sampleRate;
            out.resize (ch, (int64_t) len);
            constexpr int block = 1 << 16;
            std::vector<float*> ptrs ((size_t) ch);
            for (juce::int64 pos = 0; pos < len; pos += block)
            {
                const int n = (int) std::min<juce::int64> (block, len - pos);
                for (int c = 0; c < ch; ++c)
                    ptrs[(size_t) c] = out.channels[(size_t) c].data() + pos;
                if (! r->read (ptrs.data(), ch, pos, n))
                {
                    error = "erro ao ler " + file.getFileName();
                    return false;
                }
            }
            return true;
        };
    }
}

void StemSepProcessor::loadFile (const juce::File& file)
{
    if (isBoundToARA() || ! file.existsAsFile())
        return;
    auto e = std::make_shared<SourceEntry>();
    e->setName (file.getFileNameWithoutExtension());
    e->requestedRate = hostRate.load();
    setFileEntry (e);
    loadedFile = file;
    service->analyse (e, makeFileProvider (file), false);
}

void StemSepProcessor::reanalyse (const SourceInfo& src, bool force)
{
    if (src.araSource != nullptr)
    {
        if (auto* r = getPlaybackRenderer<StemPlaybackRenderer>())
            if (auto* dc = r->getStemDocumentController())
                dc->reanalyse (src.araSource, force);
    }
    else if (src.entry != nullptr && loadedFile.existsAsFile())
    {
        service->analyse (src.entry, makeFileProvider (loadedFile), force);
    }
}

juce::AudioProcessorEditor* StemSepProcessor::createEditor()
{
    return new StemSepEditor (*this);
}

void StemSepProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    if (auto xml = state.createXml())
    {
        xml->setAttribute ("file", isBoundToARA() ? juce::String() : loadedFile.getFullPathName());
        copyXmlToBinary (*xml, destData);
    }
}

void StemSepProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
        const juce::File f (xml->getStringAttribute ("file"));
        if (xml->getStringAttribute ("file").isNotEmpty() && f.existsAsFile())
        {
            // adiado: no momento do setState o host ainda pode nao ter ligado o ARA
            juce::MessageManager::callAsync ([f, weak = juce::WeakReference<StemSepProcessor> (this)]
            {
                if (auto* p = weak.get())
                    if (! p->isBoundToARA() && p->getLoadedFile() != f)
                        p->loadFile (f);
            });
        }
    }
}

} // namespace stemsep

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new stemsep::StemSepProcessor();
}

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<stemsep::StemDocumentController>();
}
