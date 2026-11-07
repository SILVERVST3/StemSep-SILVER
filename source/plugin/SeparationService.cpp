#include "SeparationService.h"

#include "../engine/MixConsistency.h"
#include "../engine/Resample.h"

namespace stemsep
{

QualityPreset qualityPreset (int quality)
{
    switch (quality)
    {
        case 0:  return { 2, false, "Rápida", "sobreposição 2x" };
        case 2:  return { 4, true,  "Máxima", "sobreposição 4x + TTA (3x mais lento)" };
        default: return { 4, false, "Alta",   "sobreposição 4x" };
    }
}

namespace
{
    const char* kModelTag = "scnet-xl-ihf-v1";

    std::shared_ptr<AudioData> resampleAudio (const AudioData& in, double dstRate)
    {
        auto out = std::make_shared<AudioData>();
        out->sampleRate = dstRate;
        const int64_t n = resampledLength (in.numSamples(), in.sampleRate, dstRate);
        out->resize (in.numChannels(), n);
        for (int c = 0; c < in.numChannels(); ++c)
            resampleChannel (in.channels[(size_t) c].data(), in.numSamples(), in.sampleRate,
                             out->channels[(size_t) c].data(), n, dstRate);
        return out;
    }

    bool readWav (const juce::File& f, AudioData& out, juce::String& error)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (f.createInputStream().release(), true));
        if (reader == nullptr)
        {
            error = "nao foi possivel ler " + f.getFullPathName();
            return false;
        }
        out.sampleRate = reader->sampleRate;
        out.resize ((int) reader->numChannels, (int64_t) reader->lengthInSamples);
        constexpr int block = 1 << 16;
        std::vector<float*> ptrs ((size_t) reader->numChannels);
        for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += block)
        {
            const int n = (int) std::min<juce::int64> (block, reader->lengthInSamples - pos);
            for (size_t c = 0; c < ptrs.size(); ++c)
                ptrs[c] = out.channels[c].data() + pos;
            if (! reader->read (ptrs.data(), (int) ptrs.size(), pos, n))
            {
                error = "erro de leitura em " + f.getFileName();
                return false;
            }
        }
        return true;
    }
}

bool writeWavFile (const juce::File& file, const AudioData& audio, int bits, juce::String& error)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
    if (os == nullptr)
    {
        error = "nao foi possivel criar " + file.getFullPathName();
        return false;
    }
    juce::WavAudioFormat wav;
    const auto fmt = bits == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                : juce::AudioFormatWriterOptions::SampleFormat::integral;
    const auto opts = juce::AudioFormatWriterOptions{}
                          .withSampleRate (audio.sampleRate)
                          .withNumChannels (audio.numChannels())
                          .withBitsPerSample (bits)
                          .withSampleFormat (fmt);
    auto writer = wav.createWriterFor (os, opts);
    if (writer == nullptr)
    {
        error = "formato WAV nao suportado";
        return false;
    }
    constexpr int block = 1 << 16;
    std::vector<const float*> ptrs ((size_t) audio.numChannels());
    for (int64_t pos = 0; pos < audio.numSamples(); pos += block)
    {
        const int n = (int) std::min<int64_t> (block, audio.numSamples() - pos);
        for (size_t c = 0; c < ptrs.size(); ++c)
            ptrs[c] = audio.channels[c].data() + pos;
        if (! writer->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), n))
        {
            error = "erro ao gravar " + file.getFileName();
            return false;
        }
    }
    writer.reset();
    return true;
}

//==============================================================================
SeparationService::SeparationService()
{
    loadSettings();
    ioThread.startThread (juce::Thread::Priority::low);
    sepThread.startThread (juce::Thread::Priority::low);
}

SeparationService::~SeparationService()
{
    shuttingDown = true;
    separator.abort();          // interrompe a inferencia em andamento (senao o host esperaria o bloco inteiro)
    sepThread.stop();
    ioThread.stop();
}

ServiceSettings SeparationService::getSettings() const
{
    const std::lock_guard<std::mutex> l (settingsMutex);
    return settings;
}

void SeparationService::setSettings (const ServiceSettings& s)
{
    {
        const std::lock_guard<std::mutex> l (settingsMutex);
        settings = s;
    }
    saveSettings();
}

void SeparationService::loadSettings()
{
    auto f = getDataDirectory().getChildFile ("settings.xml");
    if (auto xml = juce::XmlDocument::parse (f))
    {
        const std::lock_guard<std::mutex> l (settingsMutex);
        settings.quality    = juce::jlimit (0, 2, xml->getIntAttribute ("quality", settings.quality));
        settings.device     = juce::jlimit (0, 2, xml->getIntAttribute ("device", settings.device));
        settings.threads    = juce::jlimit (0, 64, xml->getIntAttribute ("threads", settings.threads));
        settings.exportBits = xml->getIntAttribute ("exportBits", settings.exportBits) == 24 ? 24 : 32;
    }
}

void SeparationService::saveSettings() const
{
    juce::XmlElement xml ("StemSepSettings");
    {
        const std::lock_guard<std::mutex> l (settingsMutex);
        xml.setAttribute ("quality", settings.quality);
        xml.setAttribute ("device", settings.device);
        xml.setAttribute ("threads", settings.threads);
        xml.setAttribute ("exportBits", settings.exportBits);
    }
    auto dir = getDataDirectory();
    dir.createDirectory();
    xml.writeTo (dir.getChildFile ("settings.xml"));
}

//==============================================================================
juce::File SeparationService::getDataDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("StemSep SILVER");
}

juce::File SeparationService::getCacheDirectory()
{
    return getDataDirectory().getChildFile ("Cache");
}

namespace
{
    juce::Array<juce::File> searchDirectories()
    {
        juce::Array<juce::File> dirs;
        const auto binary = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        // VST3: <bundle>.vst3/Contents/<arch>/<binario>  ->  <bundle>.vst3/Contents/Resources
        dirs.add (binary.getParentDirectory().getParentDirectory().getChildFile ("Resources"));
        dirs.add (binary.getParentDirectory());
        dirs.add (SeparationService::getDataDirectory());
        dirs.add (juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory).getChildFile ("StemSep SILVER"));
        return dirs;
    }
}

juce::File SeparationService::findModelFile()
{
    for (auto& d : searchDirectories())
    {
        auto f = d.getChildFile (modelFileName());
        if (f.existsAsFile())
            return f;
    }
    return {};
}

juce::File SeparationService::findRuntimeFile()
{
   #if JUCE_WINDOWS
    const juce::StringArray names { "onnxruntime.dll" };
   #elif JUCE_MAC
    const juce::StringArray names { "libonnxruntime.dylib" };
   #else
    const juce::StringArray names { "libonnxruntime.so", "libonnxruntime.so.1", "libonnxruntime.so.1.22.0" };
   #endif
    for (auto& d : searchDirectories())
        for (auto& n : names)
        {
            auto f = d.getChildFile (n);
            if (f.existsAsFile())
                return f;
        }
    return {};
}

juce::int64 SeparationService::getCacheSizeBytes() const
{
    juce::int64 total = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (getCacheDirectory(), true, "*", juce::File::findFiles))
        total += entry.getFileSize();
    return total;
}

void SeparationService::clearCache()
{
    getCacheDirectory().deleteRecursively();
}

juce::String SeparationService::getLastDevice() const
{
    const std::lock_guard<std::mutex> l (deviceMutex);
    return lastDevice;
}

//==============================================================================
juce::String SeparationService::computeKey (const AudioData& mix, int quality)
{
    // FNV-1a 64 bits sobre as amostras (identifica o conteudo, nao o nome do arquivo)
    uint64_t h = 1469598103934665603ULL;
    auto mixBytes = [&h] (const void* p, size_t n)
    {
        const auto* b = static_cast<const uint8_t*> (p);
        for (size_t i = 0; i < n; ++i)
        {
            h ^= b[i];
            h *= 1099511628211ULL;
        }
    };
    const double sr = mix.sampleRate;
    const int ch = mix.numChannels();
    const int64_t ns = mix.numSamples();
    mixBytes (&sr, sizeof (sr));
    mixBytes (&ch, sizeof (ch));
    mixBytes (&ns, sizeof (ns));
    for (auto& c : mix.channels)
        mixBytes (c.data(), c.size() * sizeof (float));

    const auto preset = qualityPreset (quality);
    return juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 16)
         + "_" + kModelTag + "_ov" + juce::String (preset.overlap) + (preset.tta ? "tta" : "");
}

bool SeparationService::loadFromCache (const juce::String& key, const AudioData& mix, StemArray& stems,
                                       NullReport& nr, juce::String& info)
{
    auto dir = getCacheDirectory().getChildFile (key);
    auto infoFile = dir.getChildFile ("info.xml");
    if (! infoFile.existsAsFile())
        return false;
    for (int s = 0; s < kNumStems; ++s)
    {
        juce::String err;
        if (! readWav (dir.getChildFile (juce::String (stemFileName (s)) + ".wav"), stems[(size_t) s], err))
            return false;
        if (stems[(size_t) s].numSamples() != mix.numSamples()
            || stems[(size_t) s].numChannels() != std::min (mix.numChannels(), 2))
            return false;
        stems[(size_t) s].sampleRate = mix.sampleRate;
    }
    if (auto xml = juce::XmlDocument::parse (infoFile))
    {
        nr.residualPeakDbfs = xml->getDoubleAttribute ("nullPeak", -400.0);
        nr.residualRelDb = xml->getDoubleAttribute ("nullRel", -400.0);
        nr.valid = true;
        info = xml->getStringAttribute ("info") + " (do cache)";
    }
    dir.setLastModificationTime (juce::Time::getCurrentTime());
    return true;
}

bool SeparationService::storeToCache (const juce::String& key, const StemArray& stems, const NullReport& nr,
                                      const juce::String& info)
{
    auto root = getCacheDirectory();
    root.createDirectory();
    auto tmp = root.getChildFile (key + ".tmp");
    tmp.deleteRecursively();
    if (! tmp.createDirectory())
        return false;
    for (int s = 0; s < kNumStems; ++s)
    {
        juce::String err;
        if (! writeWavFile (tmp.getChildFile (juce::String (stemFileName (s)) + ".wav"), stems[(size_t) s], 32, err))
        {
            tmp.deleteRecursively();
            return false;
        }
    }
    juce::XmlElement xml ("StemSepCache");
    xml.setAttribute ("nullPeak", nr.residualPeakDbfs);
    xml.setAttribute ("nullRel", nr.residualRelDb);
    xml.setAttribute ("info", info);
    xml.writeTo (tmp.getChildFile ("info.xml"));
    auto dst = root.getChildFile (key);
    dst.deleteRecursively();
    return tmp.moveFileTo (dst);
}

bool SeparationService::shouldStop (const SourceEntry& e, int generation) const
{
    return shuttingDown.load() || ! e.alive.load() || e.generation.load() != generation;
}

//==============================================================================
void SeparationService::cancel (SourceEntry& entry)
{
    ++entry.generation;
    if (runningEntry.load() == &entry)
        separator.abort();
}

void SeparationService::analyse (SourceEntryPtr entry, AudioProvider provider, bool force)
{
    const int gen = ++entry->generation;
    if (runningEntry.load() == entry.get())
        separator.abort();   // a analise anterior desta fonte deixa de valer
    entry->setState (EntryState::Reading);
    entry->progress = 0.0f;
    entry->setStatus (juce::String::fromUTF8 ("Lendo o áudio..."));

    ioThread.add ([this, entry, gen, provider = std::move (provider), force]
    {
        if (shouldStop (*entry, gen))
            return;

        AudioData mix;
        juce::String err;
        if (! provider (mix, err))
        {
            if (! shouldStop (*entry, gen))
            {
                entry->setState (EntryState::Error);
                entry->setStatus (juce::String::fromUTF8 ("Erro ao ler o áudio: ") + err);
            }
            return;
        }
        if (mix.numSamples() < 4096 || mix.numChannels() < 1)
        {
            entry->setState (EntryState::Error);
            entry->setStatus (juce::String::fromUTF8 ("Áudio curto demais para separar"));
            return;
        }

        auto mixPtr = std::make_shared<const AudioData> (std::move (mix));
        const int quality = getSettings().quality;
        const auto key = computeKey (*mixPtr, quality);

        // enquanto separa, o plugin toca o audio original (passagem direta)
        {
            const std::lock_guard<std::mutex> l (entry->workMutex);
            const StemData* cur = entry->peek (0);
            if (cur == nullptr || cur->numSamples != mixPtr->numSamples() || cur->numChannels != mixPtr->numChannels())
            {
                auto d = std::make_unique<StemData>();
                d->sampleRate = mixPtr->sampleRate;
                d->numChannels = mixPtr->numChannels();
                d->numSamples = mixPtr->numSamples();
                d->mix = mixPtr;
                entry->publish (0, std::move (d));
                entry->publish (1, nullptr);
            }
        }
        makeVariant (*entry, entry->requestedRate.load());

        if (shouldStop (*entry, gen))
            return;

        if (force)
        {
            getCacheDirectory().getChildFile (key).deleteRecursively();
        }
        else
        {
            entry->setStatus (juce::String::fromUTF8 ("Procurando no cache..."));
            auto stems = std::make_shared<StemArray>();
            NullReport nr;
            juce::String info;
            if (loadFromCache (key, *mixPtr, *stems, nr, info))
            {
                auto d = std::make_unique<StemData>();
                d->sampleRate = mixPtr->sampleRate;
                d->numChannels = std::min (mixPtr->numChannels(), 2);
                d->numSamples = mixPtr->numSamples();
                d->mix = mixPtr;
                d->stems = stems;
                d->nullTest = nr;
                {
                    const std::lock_guard<std::mutex> l (entry->workMutex);
                    entry->publish (0, std::move (d));
                }
                entry->setInfo (info);
                entry->progress = 1.0f;
                entry->setState (EntryState::Ready);
                entry->setStatus ("Pronto");
                makeVariant (*entry, entry->requestedRate.load());
                return;
            }
        }

        entry->setState (EntryState::Waiting);
        entry->setStatus (juce::String::fromUTF8 ("Na fila de separação"));
        sepThread.add ([this, entry, gen, mixPtr, key, quality]
        {
            runSeparation (entry, gen, mixPtr, key, quality);
        });
    });
}

void SeparationService::runSeparation (SourceEntryPtr entry, int gen, std::shared_ptr<const AudioData> mix,
                                       juce::String key, int quality)
{
    if (shouldStop (*entry, gen))
        return;

    entry->setState (EntryState::Loading);
    entry->setStatus (juce::String::fromUTF8 ("Carregando o modelo de IA..."));

    const auto modelFile = findModelFile();
    const auto runtimeFile = findRuntimeFile();
    if (! modelFile.existsAsFile() || ! runtimeFile.existsAsFile())
    {
        entry->setState (EntryState::Error);
        entry->setStatus (! modelFile.existsAsFile()
                              ? "Modelo " + modelFileName() + juce::String::fromUTF8 (" não encontrado (pasta Resources do plugin)")
                              : juce::String::fromUTF8 ("ONNX Runtime não encontrado (pasta Resources do plugin)"));
        return;
    }

    const auto st = getSettings();
    const Device dev = st.device == 1 ? Device::Cpu : st.device == 2 ? Device::Gpu : Device::Auto;
    std::string err;
    if (! separator.prepare (runtimeFile.getFullPathName().toStdString(), modelFile.getFullPathName().toStdString(),
                             dev, st.threads, err))
    {
        entry->setState (EntryState::Error);
        entry->setStatus (juce::String::fromUTF8 ("Falha ao carregar o modelo: ") + juce::String (err));
        return;
    }
    {
        const std::lock_guard<std::mutex> l (deviceMutex);
        lastDevice = separator.deviceDescription();
    }

    const auto preset = qualityPreset (quality);
    SeparationOptions opts;
    opts.overlap = preset.overlap;
    opts.tta = preset.tta;
    opts.mixConsistency = true;

    entry->setState (EntryState::Separating);
    auto progress = [entry] (double f, const std::string& s)
    {
        entry->progress = (float) f;
        entry->setStatus (juce::String (s));
    };
    auto cancel = [this, entry, gen] { return shouldStop (*entry, gen); };

    SeparationResult res;
    runningEntry = entry.get();
    if (cancel())   // cancelado entre a fila e o inicio
    {
        runningEntry = nullptr;
        return;
    }
    bool ok = separator.separate (*mix, opts, res, progress, cancel, err);

    // GPU sem memoria / driver com problema: tenta de novo na CPU (modo Auto)
    if (! ok && ! cancel() && dev == Device::Auto && separator.deviceDescription().find ("GPU") != std::string::npos)
    {
        entry->setStatus (juce::String::fromUTF8 ("A GPU falhou, continuando na CPU..."));
        std::string err2;
        if (separator.prepare (runtimeFile.getFullPathName().toStdString(), modelFile.getFullPathName().toStdString(),
                               Device::Cpu, st.threads, err2))
        {
            const std::lock_guard<std::mutex> l (deviceMutex);
            lastDevice = separator.deviceDescription();
            ok = separator.separate (*mix, opts, res, progress, cancel, err);
        }
    }

    runningEntry = nullptr;

    if (! ok)
    {
        if (! cancel())
        {
            entry->setState (EntryState::Error);
            entry->setStatus (juce::String::fromUTF8 ("Erro na separação: ") + juce::String (err));
        }
        return;
    }

    auto stems = std::make_shared<StemArray> (std::move (res.stems));
    const juce::String info = juce::String (res.deviceUsed) + ", " + juce::String::fromUTF8 (preset.name) + ", "
                            + juce::String (res.secondsElapsed / 60.0, 1) + " min";

    auto d = std::make_unique<StemData>();
    d->sampleRate = mix->sampleRate;
    d->numChannels = std::min (mix->numChannels(), 2);
    d->numSamples = mix->numSamples();
    d->mix = mix;
    d->stems = stems;
    d->nullTest = res.nullTest;
    {
        const std::lock_guard<std::mutex> l (entry->workMutex);
        entry->publish (0, std::move (d));
    }
    entry->setInfo (info);
    entry->progress = 1.0f;
    entry->setState (EntryState::Ready);
    entry->setStatus ("Pronto");

    const NullReport nr = res.nullTest;
    ioThread.add ([this, entry, stems, key, nr, info]
    {
        makeVariant (*entry, entry->requestedRate.load());
        if (! shuttingDown.load())
            storeToCache (key, *stems, nr, info);
    });
}

void SeparationService::ensureRate (SourceEntryPtr entry, double rate)
{
    entry->requestedRate = rate;
    ioThread.add ([this, entry, rate] { makeVariant (*entry, rate); });
}

void SeparationService::makeVariant (SourceEntry& entry, double rate)
{
    if (rate <= 0.0)
        return;
    const std::lock_guard<std::mutex> l (entry.workMutex);
    const StemData* native = entry.peek (0);
    if (native == nullptr || std::abs (native->sampleRate - rate) < 0.5)
        return;
    const StemData* cur = entry.peek (1);
    if (cur != nullptr && std::abs (cur->sampleRate - rate) < 0.5 && cur->hasStems() == native->hasStems()
        && cur->numSamples == resampledLength (native->numSamples, native->sampleRate, rate))
        return;

    auto d = std::make_unique<StemData>();
    d->sampleRate = rate;
    d->numChannels = native->numChannels;
    d->mix = resampleAudio (*native->mix, rate);
    d->numSamples = d->mix->numSamples();
    if (native->hasStems())
    {
        auto st = std::make_shared<StemArray>();
        for (int s = 0; s < kNumStems; ++s)
            (*st)[(size_t) s] = std::move (*resampleAudio ((*native->stems)[(size_t) s], rate));
        d->stems = st;
        d->nullTest = native->nullTest;
    }
    entry.publish (1, std::move (d));
}

void SeparationService::exportStems (SourceEntryPtr entry, juce::File folder, juce::String baseName,
                                     std::function<void (bool, juce::String, juce::Array<juce::File>)> onDone)
{
    const int bits = getSettings().exportBits;
    ioThread.add ([entry, folder, baseName, bits, onDone]
    {
        std::shared_ptr<const StemArray> stems;
        {
            const std::lock_guard<std::mutex> l (entry->workMutex);
            if (auto* d = entry->peek (0))
                stems = d->stems;
        }
        juce::Array<juce::File> files;
        bool ok = stems != nullptr;
        juce::String msg = ok ? juce::String() : juce::String::fromUTF8 ("Os stems ainda não estão prontos.");
        if (ok)
        {
            folder.createDirectory();
            for (int s : kDisplayOrder)
            {
                auto f = folder.getChildFile (juce::File::createLegalFileName (baseName + " - " + stemFileName (s) + ".wav"));
                juce::String err;
                if (! writeWavFile (f, (*stems)[(size_t) s], bits, err))
                {
                    ok = false;
                    msg = err;
                    break;
                }
                files.add (f);
            }
            if (ok)
                msg = "Stems exportados em " + folder.getFullPathName();
        }
        juce::MessageManager::callAsync ([onDone, ok, msg, files] { if (onDone) onDone (ok, msg, files); });
    });
}

NullReport SeparationService::nullTest (SourceEntry& entry)
{
    std::shared_ptr<const AudioData> mix;
    std::shared_ptr<const StemArray> stems;
    {
        const std::lock_guard<std::mutex> l (entry.workMutex);
        if (auto* d = entry.peek (0))
        {
            mix = d->mix;
            stems = d->stems;
        }
    }
    if (mix == nullptr || stems == nullptr)
        return {};
    AudioData ref;
    ref.sampleRate = mix->sampleRate;
    ref.channels.assign (mix->channels.begin(), mix->channels.begin() + std::min (2, mix->numChannels()));
    return measureNull (ref, *stems);
}

} // namespace stemsep
