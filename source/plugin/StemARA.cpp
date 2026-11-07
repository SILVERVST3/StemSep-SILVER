#include "StemARA.h"

namespace stemsep
{

//==============================================================================
SourceEntryPtr StemDocumentController::findEntry (juce::ARAAudioSource* source) const
{
    auto it = entries.find (source);
    return it != entries.end() ? it->second : nullptr;
}

void StemDocumentController::setPlaybackRate (double rate)
{
    playbackRate = rate;
}

void StemDocumentController::willBeginEditing (juce::ARADocument*)
{
    processingLock.enterWrite();
    editing = true;
}

void StemDocumentController::didEndEditing (juce::ARADocument*)
{
    editing = false;
    processingLock.exitWrite();

    auto pending = std::move (pendingStarts);
    pendingStarts.clear();
    for (auto* src : pending)
        if (entries.count (src) != 0)
            startIfPossible (src, false);
}

void StemDocumentController::didAddAudioSourceToDocument (juce::ARADocument*, juce::ARAAudioSource* source)
{
    auto entry = std::make_shared<SourceEntry>();
    const char* name = source->getName();
    entry->setName (name != nullptr && *name != 0 ? juce::String::fromUTF8 (name) : juce::String::fromUTF8 ("Item de áudio"));
    entry->requestedRate = playbackRate.load();
    entries[source] = entry;
    startIfPossible (source, false);
}

void StemDocumentController::willDestroyAudioSource (juce::ARAAudioSource* source)
{
    auto it = entries.find (source);
    if (it != entries.end())
    {
        it->second->alive = false;
        service->cancel (*it->second);
        entries.erase (it);
    }
    pendingStarts.erase (std::remove (pendingStarts.begin(), pendingStarts.end(), source), pendingStarts.end());
}

void StemDocumentController::didEnableAudioSourceSamplesAccess (juce::ARAAudioSource* source, bool enable)
{
    if (enable)
        startIfPossible (source, false);
}

void StemDocumentController::doUpdateAudioSourceContent (juce::ARAAudioSource* source, juce::ARAContentUpdateScopes scopes)
{
    if (! scopes.affectSamples())
        return;
    if (auto entry = findEntry (source))
    {
        service->cancel (*entry);
        {
            const std::lock_guard<std::mutex> l (entry->workMutex);
            entry->clearSlots();
        }
        entry->setState (EntryState::Idle);
        entry->setStatus (juce::String::fromUTF8 ("Áudio alterado, separando de novo..."));
        startIfPossible (source, false);
    }
}

void StemDocumentController::didUpdateAudioSourceProperties (juce::ARAAudioSource* source)
{
    if (auto entry = findEntry (source))
    {
        const char* name = source->getName();
        if (name != nullptr && *name != 0)
            entry->setName (juce::String::fromUTF8 (name));
    }
}

void StemDocumentController::reanalyse (juce::ARAAudioSource* source, bool force)
{
    if (auto entry = findEntry (source))
    {
        entry->setState (EntryState::Idle);
        startIfPossible (source, force);
    }
}

void StemDocumentController::startIfPossible (juce::ARAAudioSource* source, bool force)
{
    if (editing)
    {
        if (std::find (pendingStarts.begin(), pendingStarts.end(), source) == pendingStarts.end())
            pendingStarts.push_back (source);
        return;
    }

    auto entry = findEntry (source);
    if (entry == nullptr)
        return;

    if (! source->isSampleAccessEnabled())
    {
        if (entry->getState() == EntryState::Idle)
            entry->setStatus (juce::String::fromUTF8 ("Aguardando o host liberar o áudio do item"));
        return;
    }

    const auto st = entry->getState();
    if (! force && st != EntryState::Idle && st != EntryState::Error)
        return;

    // O leitor ARA precisa ser criado e destruido no thread de mensagens; a leitura em si
    // acontece no thread de E/S.
    std::shared_ptr<juce::ARAAudioSourceReader> reader (
        new juce::ARAAudioSourceReader (source),
        [] (juce::ARAAudioSourceReader* r)
        {
            if (juce::MessageManager::existsAndIsCurrentThread())
                delete r;
            else if (! juce::MessageManager::callAsync ([r] { delete r; }))
                juce::ignoreUnused (r);   // sem thread de mensagens (encerrando): deixa vazar
        });

    auto provider = [reader] (AudioData& out, juce::String& error) -> bool
    {
        const int ch = (int) reader->numChannels;
        const juce::int64 len = reader->lengthInSamples;
        if (ch < 1 || len <= 0)
        {
            error = juce::String::fromUTF8 ("item sem áudio");
            return false;
        }
        out.sampleRate = reader->sampleRate;
        out.resize (ch, (int64_t) len);

        constexpr int block = 1 << 16;
        std::vector<float*> ptrs ((size_t) ch);
        for (juce::int64 pos = 0; pos < len; pos += block)
        {
            const int n = (int) std::min<juce::int64> (block, len - pos);
            for (int c = 0; c < ch; ++c)
                ptrs[(size_t) c] = out.channels[(size_t) c].data() + pos;

            bool ok = false;
            for (int attempt = 0; attempt < 400 && ! ok; ++attempt)
            {
                ok = reader->read (ptrs.data(), ch, pos, n);
                if (! ok)
                {
                    if (! reader->isValid())
                        break;
                    juce::Thread::sleep (25);
                }
            }
            if (! ok)
            {
                error = juce::String::fromUTF8 ("o host não liberou a leitura do item");
                return false;
            }
        }
        return true;
    };

    entry->requestedRate = playbackRate.load();
    service->analyse (entry, std::move (provider), force);
}

juce::ARAPlaybackRenderer* StemDocumentController::doCreatePlaybackRenderer() noexcept
{
    return new StemPlaybackRenderer (getDocumentController());
}

bool StemDocumentController::doRestoreObjectsFromStream (juce::ARAInputStream& input, const juce::ARARestoreObjectsFilter*) noexcept
{
    // Os stems ficam no cache em disco (identificados pelo conteudo do audio), entao o arquivo
    // do projeto guarda so a versao do formato.
    juce::ignoreUnused (input.readInt64());
    return true;
}

bool StemDocumentController::doStoreObjectsToStream (juce::ARAOutputStream& output, const juce::ARAStoreObjectsFilter*) noexcept
{
    return output.writeInt64 (1);
}

//==============================================================================
StemDocumentController* StemPlaybackRenderer::getStemDocumentController() const noexcept
{
    return juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<StemDocumentController> (getDocumentController());
}

std::vector<juce::ARAAudioSource*> StemPlaybackRenderer::getAudioSources() const
{
    std::vector<juce::ARAAudioSource*> result;
    for (auto* region : getPlaybackRegions())
    {
        auto* src = region->getAudioModification()->getAudioSource();
        if (std::find (result.begin(), result.end(), src) == result.end())
            result.push_back (src);
    }
    return result;
}

void StemPlaybackRenderer::prepareToPlay (double rate, int maximumSamplesPerBlock, int numChannels,
                                          juce::AudioProcessor::ProcessingPrecision precision, AlwaysNonRealtime anr)
{
    juce::ARAPlaybackRenderer::prepareToPlay (rate, maximumSamplesPerBlock, numChannels, precision, anr);
    sampleRate = rate;
    maxBlock = maximumSamplesPerBlock;
    ramp.initialised = false;

    if (auto* dc = getStemDocumentController())
    {
        dc->setPlaybackRate (rate);
        for (auto* src : getAudioSources())
            if (auto entry = dc->findEntry (src))
                dc->getService().ensureRate (entry, rate);
    }
}

void StemPlaybackRenderer::releaseResources()
{
    juce::ARAPlaybackRenderer::releaseResources();
}

void StemPlaybackRenderer::waitForNonRealtime (const juce::AudioPlayHead::PositionInfo& positionInfo, int numSamples)
{
    // Render offline (exportar/bounce): espera a separacao terminar para nao renderizar o
    // audio original por engano. Nunca bloqueia o thread de mensagens.
    if (juce::MessageManager::existsAndIsCurrentThread())
        return;
    auto* dc = getStemDocumentController();
    if (dc == nullptr)
        return;

    std::vector<SourceEntryPtr> waitFor;
    {
        const auto lock = dc->getProcessingLock();
        if (! lock.isLocked())
            return;
        const auto t = positionInfo.getTimeInSamples().orFallback (0);
        const auto blockRange = juce::Range<juce::int64>::withStartAndLength (t, numSamples);
        for (auto* region : getPlaybackRegions())
        {
            if (region->getSampleRange (sampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no)
                    .getIntersectionWith (blockRange).isEmpty())
                continue;
            if (auto e = dc->findEntry (region->getAudioModification()->getAudioSource()))
                waitFor.push_back (e);
        }
    }

    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (auto& e : waitFor)
    {
        while (juce::Time::getMillisecondCounterHiRes() - start < 4.0 * 3600.0 * 1000.0)
        {
            const auto st = e->getState();
            if (! e->alive.load() || st == EntryState::Error || st == EntryState::Idle)
                break;
            if (st == EntryState::Ready)
            {
                SourceEntry::ReadScope rs (*e);
                if (auto* d = rs.find (sampleRate))
                    if (d->hasStems())
                        break;
            }
            juce::Thread::sleep (50);
        }
    }
}

bool StemPlaybackRenderer::processBlock (juce::AudioBuffer<float>& buffer, juce::AudioProcessor::Realtime realtime,
                                         const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept
{
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    auto* dc = getStemDocumentController();
    if (dc == nullptr)
        return true;

    if (realtime == juce::AudioProcessor::Realtime::no && positionInfo.getIsPlaying())
        waitForNonRealtime (positionInfo, numSamples);

    const auto lock = dc->getProcessingLock();
    if (! lock.isLocked())
        return true;

    MixControls* ctl = controls.load();
    float targets[kNumStems] { 1.0f, 1.0f, 1.0f, 1.0f };
    if (ctl != nullptr)
        ctl->computeTargets (targets);
    ramp.setTargets (targets);

    if (positionInfo.getIsPlaying())
    {
        const auto blockStart = positionInfo.getTimeInSamples().orFallback (0);
        const auto blockRange = juce::Range<juce::int64>::withStartAndLength (blockStart, numSamples);

        for (auto* region : getPlaybackRegions())
        {
            const auto playRange = region->getSampleRange (sampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
            auto renderRange = blockRange.getIntersectionWith (playRange);
            if (renderRange.isEmpty())
                continue;

            // posicao no audio de origem (em amostras da taxa de reproducao)
            const double modStartT = region->getStartInAudioModificationTime();
            const double modEndT = region->getEndInAudioModificationTime();
            const double playStartT = region->getStartInPlaybackTime();
            const auto offset = (juce::int64) std::llround ((modStartT - playStartT) * sampleRate);
            const auto modRange = juce::Range<juce::int64> ((juce::int64) std::llround (modStartT * sampleRate),
                                                            (juce::int64) std::llround (modEndT * sampleRate));
            renderRange = renderRange.getIntersectionWith (modRange - offset);
            if (renderRange.isEmpty())
                continue;

            auto it = dc->findEntry (region->getAudioModification()->getAudioSource());
            if (it == nullptr)
                continue;

            SourceEntry::ReadScope rs (*it);
            const StemData* data = rs.find (sampleRate);
            if (data == nullptr)
                continue;

            renderStemData (*data, renderRange.getStart() + offset, buffer,
                            (int) (renderRange.getStart() - blockStart), (int) renderRange.getLength(),
                            ramp, numSamples, ctl);
        }
    }

    ramp.advance();
    return true;
}

} // namespace stemsep
