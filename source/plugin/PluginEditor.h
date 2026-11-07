#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "SilverLookAndFeel.h"

namespace stemsep
{

// Alca para arrastar um stem direto para a timeline do DAW
class DragHandle : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    std::function<void()> onDragStart;
    juce::Colour accent { Silver::navy };
    bool enabledForDrag = false;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override { dragging = false; }
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    bool dragging = false;
};

class StemStrip : public juce::Component
{
public:
    StemStrip (StemSepProcessor& p, int stemIndex);

    void paint (juce::Graphics&) override;
    void resized() override;

    void updateMeter (float peakLinear);
    void setDragEnabled (bool e) { drag.enabledForDrag = e; drag.repaint(); }

    std::function<void (int stem)> onDragOut;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    int stem;
    juce::Colour accent;
    juce::Slider fader;
    juce::TextButton mute { "M" }, solo { "S" };
    DragHandle drag;
    std::unique_ptr<SliderAttachment> faderAtt;
    std::unique_ptr<ButtonAttachment> muteAtt, soloAtt;
    float meterDb = -90.0f;
    juce::Rectangle<int> meterArea;
};

class StemSepEditor : public juce::AudioProcessorEditor,
                      public juce::AudioProcessorEditorARAExtension,
                      public juce::DragAndDropContainer,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    explicit StemSepEditor (StemSepProcessor&);
    ~StemSepEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void refreshSources();
    StemSepProcessor::SourceInfo currentSource() const;
    void exportStems();
    void runNullTest();
    void dragStem (int stem);
    void openFile();
    void updateCacheButton();

    StemSepProcessor& proc;
    SilverLookAndFeel lnf;

    juce::ComboBox qualityBox, deviceBox, sourceBox;
    juce::Label qualityLabel, deviceLabel, sourceLabel;
    juce::TextButton openButton, exportButton, reanalyseButton, nullButton, cacheButton;
    std::array<std::unique_ptr<StemStrip>, kNumStems> strips;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    std::vector<StemSepProcessor::SourceInfo> sources;
    juce::StringArray sourceNames;
    int timerTicks = 0;

    // estado mostrado
    juce::String statusText, infoText, nullText, modeText;
    juce::Colour nullColour { Silver::navySoft };
    float progress = 0.0f;
    EntryState state = EntryState::Idle;
    juce::Rectangle<int> statusArea, progressArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSepEditor)
};

} // namespace stemsep
