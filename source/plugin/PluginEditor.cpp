#include "PluginEditor.h"

namespace stemsep
{

namespace
{
    juce::String stemDisplayName (int stem)
    {
        switch (stem)
        {
            case kDrums:  return U8 ("Bateria");
            case kBass:   return U8 ("Baixo");
            case kVocals: return U8 ("Vocal");
            default:      return U8 ("Instrumentos");
        }
    }

    juce::String formatDbfs (double db)
    {
        if (db <= -399.0) return U8 ("-∞ dBFS");
        return juce::String (db, 1) + " dBFS";
    }
}

//==============================================================================
void DragHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool over = isMouseOver() && enabledForDrag;
    g.setColour (enabledForDrag ? (over ? accent.withAlpha (0.18f) : Silver::metalLight) : Silver::metalDark.withAlpha (0.5f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (enabledForDrag ? accent : Silver::panelEdge);
    const float dashes[] { 4.0f, 3.0f };
    juce::Path p;
    p.addRoundedRectangle (r, 5.0f);
    juce::PathStrokeType (1.0f).createDashedStroke (p, p, dashes, 2);
    g.fillPath (p);
    g.setColour (enabledForDrag ? Silver::navy : Silver::navySoft.withAlpha (0.5f));
    g.setFont (SilverLookAndFeel::uiFont (12.5f, true));
    g.drawText (U8 ("⇩ Arrastar p/ DAW"), getLocalBounds(), juce::Justification::centred);
}

void DragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (! enabledForDrag || dragging || e.getDistanceFromDragStart() < 6)
        return;
    dragging = true;
    if (onDragStart)
        onDragStart();
}

//==============================================================================
StemStrip::StemStrip (StemSepProcessor& p, int stemIndex)
    : stem (stemIndex), accent (Silver::stemColour (stemIndex))
{
    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
    fader.setColour (juce::Slider::trackColourId, accent);
    fader.setColour (juce::Slider::textBoxTextColourId, Silver::navy);
    fader.setColour (juce::Slider::textBoxBackgroundColourId, Silver::metalLight);
    fader.setColour (juce::Slider::textBoxOutlineColourId, Silver::panelEdge);
    fader.setColour (juce::Slider::textBoxHighlightColourId, Silver::navySoft.withAlpha (0.3f));
    fader.setTextValueSuffix (" dB");
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setTooltip (U8 ("Volume do stem (duplo clique = 0 dB)"));
    addAndMakeVisible (fader);

    mute.setClickingTogglesState (true);
    solo.setClickingTogglesState (true);
    mute.getProperties().set ("accent", (juce::int64) Silver::bad.getARGB());
    solo.getProperties().set ("accent", (juce::int64) Silver::warn.getARGB());
    mute.setTooltip ("Mute");
    solo.setTooltip ("Solo");
    addAndMakeVisible (mute);
    addAndMakeVisible (solo);

    drag.accent = accent;
    drag.onDragStart = [this] { if (onDragOut) onDragOut (stem); };
    drag.setTooltip (U8 ("Arraste este stem para a timeline do REAPER (cria um WAV 32 bits)"));
    addAndMakeVisible (drag);

    auto& apvts = p.getParameters();
    faderAtt = std::make_unique<SliderAttachment> (apvts, StemSepProcessor::paramId ("gain", stem), fader);
    muteAtt = std::make_unique<ButtonAttachment> (apvts, StemSepProcessor::paramId ("mute", stem), mute);
    soloAtt = std::make_unique<ButtonAttachment> (apvts, StemSepProcessor::paramId ("solo", stem), solo);
    fader.textFromValueFunction = [] (double v) { return v <= -59.9 ? U8 ("-∞") : juce::String (v, 1); };
    fader.updateText();
}

void StemStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Silver::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Silver::panelEdge);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    // faixa de cor + nome
    auto head = getLocalBounds().removeFromTop (34).reduced (8, 6);
    g.setColour (accent);
    g.fillRoundedRectangle (head.removeFromLeft (6).toFloat(), 3.0f);
    head.removeFromLeft (8);
    g.setColour (Silver::navy);
    g.setFont (SilverLookAndFeel::uiFont (17.0f, true));
    g.drawText (stemDisplayName (stem), head, juce::Justification::centredLeft);

    // medidor
    auto m = meterArea.toFloat();
    g.setColour (Silver::navy.withAlpha (0.08f));
    g.fillRoundedRectangle (m, 3.0f);
    const float norm = juce::jlimit (0.0f, 1.0f, (meterDb + 60.0f) / 66.0f);
    auto lit = m.withTop (m.getBottom() - m.getHeight() * norm);
    g.setGradientFill (juce::ColourGradient (accent.brighter (0.3f), 0, m.getBottom(), accent.darker (0.2f), 0, m.getY(), false));
    g.fillRoundedRectangle (lit, 3.0f);
    if (meterDb > -0.1f)
    {
        g.setColour (Silver::bad);
        g.fillRect (m.withHeight (4.0f));
    }
    g.setColour (Silver::panelEdge);
    g.drawRoundedRectangle (m, 3.0f, 1.0f);
}

void StemStrip::resized()
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (28);
    auto bottom = r.removeFromBottom (30);
    r.removeFromBottom (8);
    auto buttons = r.removeFromBottom (28);
    r.removeFromBottom (6);

    drag.setBounds (bottom);
    const int bw = (buttons.getWidth() - 8) / 2;
    mute.setBounds (buttons.removeFromLeft (bw));
    buttons.removeFromLeft (8);
    solo.setBounds (buttons);

    meterArea = r.removeFromLeft (14).withTrimmedBottom (24).withTrimmedTop (4);
    r.removeFromLeft (4);
    fader.setBounds (r);
}

void StemStrip::updateMeter (float peak)
{
    const float db = peak > 1.0e-6f ? juce::Decibels::gainToDecibels (peak) : -90.0f;
    const float next = db > meterDb ? db : juce::jmax (db, meterDb - 2.5f);
    if (std::abs (next - meterDb) > 0.05f)
    {
        meterDb = next;
        repaint (meterArea.expanded (2));
    }
}

//==============================================================================
StemSepEditor::StemSepEditor (StemSepProcessor& p)
    : juce::AudioProcessorEditor (&p), juce::AudioProcessorEditorARAExtension (&p), proc (p)
{
    setLookAndFeel (&lnf);

    auto settings = proc.getService().getSettings();

    qualityLabel.setText (U8 ("Qualidade"), juce::dontSendNotification);
    deviceLabel.setText (U8 ("Processamento"), juce::dontSendNotification);
    for (auto* l : { &qualityLabel, &deviceLabel, &sourceLabel })
    {
        l->setColour (juce::Label::textColourId, Silver::navySoft);
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
    }

    qualityBox.addItem (U8 ("Rápida (2x)"), 1);
    qualityBox.addItem (U8 ("Alta (4x) - padrão"), 2);
    qualityBox.addItem (U8 ("Máxima (4x + TTA)"), 3);
    qualityBox.setSelectedId (settings.quality + 1, juce::dontSendNotification);
    qualityBox.setTooltip (U8 ("Rápida: menos sobreposição. Alta: padrão do modelo. Máxima: média de 3 passadas "
                               "(canais trocados e polaridade invertida) - menos artefatos, 3x mais lenta. "
                               "Vale para as próximas análises (use Reanalisar)."));
    qualityBox.onChange = [this]
    {
        auto s = proc.getService().getSettings();
        s.quality = qualityBox.getSelectedId() - 1;
        proc.getService().setSettings (s);
    };
    addAndMakeVisible (qualityBox);

    deviceBox.addItem (U8 ("Auto (GPU se houver)"), 1);
    deviceBox.addItem (U8 ("Somente CPU"), 2);
    deviceBox.addItem (U8 ("GPU (DirectML)"), 3);
    deviceBox.setSelectedId (settings.device + 1, juce::dontSendNotification);
    deviceBox.setTooltip (U8 ("Auto usa a placa de vídeo via DirectML (ex.: Radeon RX 580) e cai para a CPU se ela falhar."));
    deviceBox.onChange = [this]
    {
        auto s = proc.getService().getSettings();
        s.device = deviceBox.getSelectedId() - 1;
        proc.getService().setSettings (s);
    };
    addAndMakeVisible (deviceBox);

    sourceBox.setTextWhenNothingSelected (U8 ("(nenhum item)"));
    sourceBox.setTextWhenNoChoicesAvailable (U8 ("(nenhum item)"));
    addAndMakeVisible (sourceBox);

    openButton.setButtonText (U8 ("Abrir áudio..."));
    openButton.onClick = [this] { openFile(); };
    addAndMakeVisible (openButton);

    exportButton.setButtonText (U8 ("Exportar stems..."));
    exportButton.onClick = [this] { exportStems(); };
    exportButton.setTooltip (U8 ("Grava Bateria, Baixo, Vocal e Instrumentos em WAV (32 bits float por padrão)."));
    addAndMakeVisible (exportButton);

    reanalyseButton.setButtonText (U8 ("Reanalisar"));
    reanalyseButton.onClick = [this]
    {
        auto src = currentSource();
        if (src.entry != nullptr)
            proc.reanalyse (src, true);
    };
    reanalyseButton.setTooltip (U8 ("Separa de novo com a qualidade/processamento escolhidos (ignora o cache)."));
    addAndMakeVisible (reanalyseButton);

    nullButton.setButtonText (U8 ("Teste de nulidade"));
    nullButton.onClick = [this] { runNullTest(); };
    nullButton.setTooltip (U8 ("Soma os 4 stems com a polaridade invertida do original: se cancelar, nada se perdeu."));
    addAndMakeVisible (nullButton);

    cacheButton.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, U8 ("Limpar cache"),
            U8 ("Apagar os stems guardados em disco? As músicas precisarão ser separadas de novo."),
            U8 ("Apagar"), U8 ("Cancelar"), this,
            juce::ModalCallbackFunction::create ([this] (int r)
            {
                if (r == 1)
                {
                    proc.getService().clearCache();
                    updateCacheButton();
                }
            }));
    };
    cacheButton.setTooltip (SeparationService::getCacheDirectory().getFullPathName());
    addAndMakeVisible (cacheButton);

    for (int s : kDisplayOrder)
    {
        strips[(size_t) s] = std::make_unique<StemStrip> (proc, s);
        strips[(size_t) s]->onDragOut = [this] (int stem) { dragStem (stem); };
        addAndMakeVisible (*strips[(size_t) s]);
    }

    updateCacheButton();
    setSize (880, 580);
    refreshSources();
    startTimerHz (15);
}

StemSepEditor::~StemSepEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void StemSepEditor::updateCacheButton()
{
    const double gb = (double) proc.getService().getCacheSizeBytes() / (1024.0 * 1024.0 * 1024.0);
    cacheButton.setButtonText (U8 ("Limpar cache (") + juce::String (gb, 2) + " GB)");
}

StemSepProcessor::SourceInfo StemSepEditor::currentSource() const
{
    const int idx = sourceBox.getSelectedItemIndex();
    if (idx >= 0 && idx < (int) sources.size())
        return sources[(size_t) idx];
    return sources.empty() ? StemSepProcessor::SourceInfo {} : sources.front();
}

void StemSepEditor::refreshSources()
{
    auto now = proc.getSources();
    juce::StringArray names;
    for (auto& s : now)
        names.add (s.entry->getName());

    const bool changed = names != sourceNames || now.size() != sources.size();
    sources = std::move (now);
    if (changed)
    {
        const auto prev = sourceBox.getText();
        sourceNames = names;
        sourceBox.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i)
            sourceBox.addItem (names[i], i + 1);
        int sel = names.indexOf (prev);
        if (sel < 0 && names.size() > 0) sel = 0;
        if (sel >= 0) sourceBox.setSelectedItemIndex (sel, juce::dontSendNotification);
    }

    const bool ara = proc.isARAMode();
    sourceLabel.setText (ara ? U8 ("Item (ARA)") : U8 ("Arquivo"), juce::dontSendNotification);
    openButton.setVisible (! ara);
    modeText = ara ? U8 ("Modo ARA: o plugin lê o item da faixa e toca os stems no lugar do original.")
                   : U8 ("Modo arquivo (sem ARA): abra ou arraste um áudio; os stems tocam a partir do início do projeto.");
}

void StemSepEditor::timerCallback()
{
    if (++timerTicks % 15 == 1)
        refreshSources();
    if (timerTicks % 150 == 2)
        updateCacheButton();

    auto& ctl = proc.getMixControls();
    for (int s = 0; s < kNumStems; ++s)
        strips[(size_t) s]->updateMeter (ctl.meters[(size_t) s].exchange (0.0f));

    auto src = currentSource();
    juce::String newStatus, newInfo, newNull;
    juce::Colour newNullColour = Silver::navySoft;
    float newProgress = 0.0f;
    EntryState newState = EntryState::Idle;
    bool hasStems = false;

    if (src.entry == nullptr)
    {
        newStatus = proc.isARAMode() ? U8 ("Nenhum item nesta faixa. Coloque um item de áudio na faixa do plugin.")
                                     : U8 ("Abra ou arraste um arquivo de áudio para separar.");
    }
    else
    {
        newState = src.entry->getState();
        newProgress = src.entry->progress.load();
        newStatus = src.entry->getStatus();
        newInfo = src.entry->getInfo();
        {
            // try_lock: se um thread de trabalho estiver usando a entrada, mantem o que ja esta na tela
            std::unique_lock<std::mutex> l (src.entry->workMutex, std::try_to_lock);
            if (! l.owns_lock())
                return;
            if (auto* d = src.entry->peek (0))
            {
                hasStems = d->hasStems();
                if (hasStems && d->nullTest.valid)
                {
                    const bool ok = d->nullTest.residualPeakDbfs < -100.0;
                    newNull = U8 ("Nulidade: ") + formatDbfs (d->nullTest.residualPeakDbfs) + (ok ? U8 ("  ✓") : U8 ("  !"));
                    newNullColour = ok ? Silver::good : Silver::warn;
                }
            }
        }
    }

    for (auto& s : strips)
        s->setDragEnabled (hasStems);
    exportButton.setEnabled (hasStems);
    nullButton.setEnabled (hasStems);
    reanalyseButton.setEnabled (src.entry != nullptr);

    if (newStatus != statusText || newInfo != infoText || newNull != nullText || std::abs (newProgress - progress) > 0.001f
        || newState != state)
    {
        statusText = newStatus;
        infoText = newInfo;
        nullText = newNull;
        nullColour = newNullColour;
        progress = newProgress;
        state = newState;
        repaint (statusArea);
    }
}

void StemSepEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (Silver::bgTop, 0, 0, Silver::bgBottom, 0, (float) getHeight(), false));
    g.fillAll();

    // brilho metalico no topo
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillRect (0, 0, getWidth(), 2);

    g.setColour (Silver::navy);
    g.setFont (SilverLookAndFeel::uiFont (28.0f, true));
    g.drawText ("StemSep", 22, 12, 140, 34, juce::Justification::centredLeft);
    g.setColour (Silver::navySoft);
    g.drawText ("SILVER", 146, 12, 120, 34, juce::Justification::centredLeft);
    g.setFont (SilverLookAndFeel::uiFont (13.0f));
    g.drawText (U8 ("Separação de stems por IA  •  Bateria · Baixo · Vocal · Instrumentos"), 24, 44, 520, 18,
                juce::Justification::centredLeft);

    // painel de status
    auto panel = statusArea.toFloat();
    g.setColour (Silver::panel);
    g.fillRoundedRectangle (panel, 8.0f);
    g.setColour (Silver::panelEdge);
    g.drawRoundedRectangle (panel, 8.0f, 1.0f);

    auto inner = statusArea.reduced (14, 10);
    auto line1 = inner.removeFromTop (22);
    g.setColour (state == EntryState::Error ? Silver::bad : Silver::navy);
    g.setFont (SilverLookAndFeel::uiFont (15.0f, true));
    g.drawText (statusText, line1.withTrimmedRight (240), juce::Justification::centredLeft, true);
    if (nullText.isNotEmpty())
    {
        g.setColour (nullColour);
        g.setFont (SilverLookAndFeel::uiFont (15.0f, true));
        g.drawText (nullText, line1.removeFromRight (240), juce::Justification::centredRight);
    }

    inner.removeFromTop (6);
    auto bar = inner.removeFromTop (12).toFloat();
    g.setColour (Silver::navy.withAlpha (0.10f));
    g.fillRoundedRectangle (bar, 6.0f);
    const float shown = state == EntryState::Ready ? 1.0f : progress;
    if (shown > 0.0f)
    {
        auto fill = bar.withWidth (juce::jmax (12.0f, bar.getWidth() * shown));
        g.setGradientFill (juce::ColourGradient (Silver::navySoft, fill.getX(), 0, Silver::navy, fill.getRight(), 0, false));
        g.fillRoundedRectangle (fill, 6.0f);
    }

    inner.removeFromTop (6);
    g.setColour (Silver::navySoft);
    g.setFont (SilverLookAndFeel::uiFont (12.5f));
    g.drawText (infoText.isNotEmpty() ? infoText : modeText, inner.removeFromTop (18), juce::Justification::centredLeft, true);
    g.drawText (infoText.isNotEmpty() ? modeText : juce::String(), inner.removeFromTop (18), juce::Justification::centredLeft, true);
}

void StemSepEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 0);

    auto header = r.removeFromTop (72);
    auto right = header.removeFromRight (420).withTrimmedTop (14).withHeight (46);
    auto devCol = right.removeFromRight (200);
    right.removeFromRight (14);
    qualityLabel.setBounds (right.removeFromTop (18));
    qualityBox.setBounds (right.removeFromTop (28));
    deviceLabel.setBounds (devCol.removeFromTop (18));
    deviceBox.setBounds (devCol.removeFromTop (28));

    auto row = r.removeFromTop (32);
    sourceLabel.setBounds (row.removeFromLeft (84));
    openButton.setBounds (row.removeFromRight (140));
    row.removeFromRight (10);
    sourceBox.setBounds (row);

    r.removeFromTop (10);
    statusArea = r.removeFromTop (96);
    r.removeFromTop (12);

    auto footer = r.removeFromBottom (54).withTrimmedTop (10).withTrimmedBottom (8);
    const int gap = 10;
    const int bw = (footer.getWidth() - 3 * gap) / 4;
    exportButton.setBounds (footer.removeFromLeft (bw));
    footer.removeFromLeft (gap);
    reanalyseButton.setBounds (footer.removeFromLeft (bw));
    footer.removeFromLeft (gap);
    nullButton.setBounds (footer.removeFromLeft (bw));
    footer.removeFromLeft (gap);
    cacheButton.setBounds (footer);

    const int sw = (r.getWidth() - 3 * 12) / 4;
    for (size_t i = 0; i < kDisplayOrder.size(); ++i)
    {
        strips[(size_t) kDisplayOrder[i]]->setBounds (r.removeFromLeft (sw));
        r.removeFromLeft (12);
    }
}

//==============================================================================
void StemSepEditor::openFile()
{
    chooser = std::make_unique<juce::FileChooser> (U8 ("Escolha a música para separar"), juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.wma");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                              {
                                  proc.loadFile (f);
                                  refreshSources();
                              }
                          });
}

bool StemSepEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (proc.isARAMode())
        return false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;wma"))
            return true;
    return false;
}

void StemSepEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
    {
        juce::File file (f);
        if (file.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;wma"))
        {
            proc.loadFile (file);
            refreshSources();
            return;
        }
    }
}

void StemSepEditor::exportStems()
{
    auto src = currentSource();
    if (src.entry == nullptr)
        return;
    chooser = std::make_unique<juce::FileChooser> (U8 ("Pasta para os stems"),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, entry = src.entry] (const juce::FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (dir == juce::File())
                                  return;
                              statusText = U8 ("Exportando stems...");
                              repaint (statusArea);
                              juce::Component::SafePointer<StemSepEditor> safe (this);
                              proc.getService().exportStems (entry, dir, entry->getName(),
                                  [safe] (bool ok, juce::String msg, juce::Array<juce::File>)
                                  {
                                      juce::AlertWindow::showMessageBoxAsync (ok ? juce::MessageBoxIconType::InfoIcon
                                                                                 : juce::MessageBoxIconType::WarningIcon,
                                                                              U8 ("Exportar stems"), msg);
                                      juce::ignoreUnused (safe);
                                  });
                          });
}

void StemSepEditor::runNullTest()
{
    auto src = currentSource();
    if (src.entry == nullptr)
        return;
    const auto nr = SeparationService::nullTest (*src.entry);
    if (! nr.valid)
        return;
    const bool ok = nr.residualPeakDbfs < -100.0;
    juce::String msg;
    msg << U8 ("Original + (Bateria + Baixo + Vocal + Instrumentos com polaridade invertida):\n\n")
        << U8 ("Resíduo de pico: ") << formatDbfs (nr.residualPeakDbfs) << "\n"
        << U8 ("Energia do resíduo: ") << (nr.residualRelDb <= -399.0 ? U8 ("-∞") : juce::String (nr.residualRelDb, 1))
        << U8 (" dB em relação ao original\n\n")
        << (ok ? U8 ("Cancelamento total: os 4 stems somados reconstroem o original sem perdas.")
               : U8 ("Atenção: a soma não cancelou totalmente."));
    juce::AlertWindow::showMessageBoxAsync (ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                            U8 ("Teste de nulidade"), msg);
}

void StemSepEditor::dragStem (int stem)
{
    auto src = currentSource();
    if (src.entry == nullptr)
        return;
    std::shared_ptr<const StemArray> stems;
    {
        const std::lock_guard<std::mutex> l (src.entry->workMutex);
        if (auto* d = src.entry->peek (0))
            stems = d->stems;
    }
    if (stems == nullptr)
        return;

    auto dir = SeparationService::getDataDirectory().getChildFile ("Arrastados");
    dir.createDirectory();
    auto file = dir.getChildFile (juce::File::createLegalFileName (src.entry->getName() + " - " + stemFileName (stem) + ".wav"));
    juce::String err;
    {
        juce::MouseCursor::showWaitCursor();
        const bool ok = writeWavFile (file, (*stems)[(size_t) stem], 32, err);
        juce::MouseCursor::hideWaitCursor();
        if (! ok)
            return;
    }
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

} // namespace stemsep
